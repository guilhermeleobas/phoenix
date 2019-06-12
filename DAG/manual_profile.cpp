#include "llvm/ADT/Statistic.h"  // For the STATISTIC macro.
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/IR/Constants.h"          // For ConstantData, for instance.
#include "llvm/IR/DebugInfoMetadata.h"  // For DILocation
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"  // To use the iterator instructions(f)
#include "llvm/IR/Instructions.h"  // To have access to the Instructions.
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"  // To print error messages.
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"  // For dbgs()
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include <queue>

#include "../ProgramSlicing/ProgramSlicing.h"
#include "manual_profile.h"
#include "utils.h"

using namespace llvm;

#define DUMP(F, node) errs() << "[" << F->getName() << "] " << *node << "\n"

#define SWAP_SUCCESSOR(BB, VMAP)                            \
  do {                                                      \
    auto *br = BB->getTerminator();                         \
    for (unsigned i = 0; i < br->getNumSuccessors(); ++i) { \
      auto *suc = br->getSuccessor(i);                      \
      if (VMap[suc])                                        \
        br->setSuccessor(i, cast<BasicBlock>(VMap[suc]));   \
    }                                                       \
  } while (0)

namespace phoenix {

static Loop *get_outer_loop(LoopInfo *LI, BasicBlock *BB) {
  Loop *L = LI->getLoopFor(BB);

  BasicBlock *header = L->getHeader();
  unsigned depth = L->getLoopDepth();
  if (depth <= 1)
    return L;

  for (BasicBlock *pred : predecessors(header)) {
    Loop *L2 = LI->getLoopFor(pred);

    if (L2 == nullptr)
      continue;

    if (L2->getLoopDepth() < depth) {
      return get_outer_loop(LI, pred);
    }
  }

  llvm_unreachable("unreachable state");
}

static std::vector<Loop *> find_loop_chain(LoopInfo *LI, Loop *L) {
  std::vector<Loop *> v;

  while (true) {
    unsigned depth = L->getLoopDepth();

    if (depth <= 1) {
      v.push_back(L);
      break;
    }
  }
}

static std::vector<Loop *> find_loop_chain(LoopInfo *LI, BasicBlock *BB) {
  return find_loop_chain(LI, LI->getLoopFor(BB));
}

static BasicBlock *split_pre_header(Loop *L, LoopInfo *LI, DominatorTree *DT) {
  // Get the loop pre-header
  auto *ph = L->getLoopPreheader();
  // split the block and move every inst to @ph, except for the branch inst
  Instruction *SplitPtr = &*ph->begin();
  auto *pp = llvm::SplitBlock(ph, SplitPtr, DT, LI);
  std::swap(pp, ph);
  for (Instruction &I : *pp) {
    if (isa<TerminatorInst>(&I))
      break;
    I.moveBefore(&*ph->begin());
  }
  // pp is the pre preHeader
  return pp;
}

// Creates a call to the sampling function
//  - @F : The function
//  - @pp : The loop pre preheader
//  - @L/@C : Original/Cloned loops
static void fill_control(Function *F,
                         BasicBlock *pp,
                         Loop *L,
                         Loop *C,
                         LoopInfo *LI,
                         DominatorTree *DT) {
  Module *M = pp->getModule();
  Function *fn = M->getFunction("__sampling");

  llvm::SmallVector<Value *, 2> args;
  for (Argument &arg : F->args())
    args.push_back(&arg);

  IRBuilder<> Builder(pp->getFirstNonPHI());
  Value *n_zeros = Builder.CreateCall(fn, args, "n_zeros", nullptr);

  // Create the conditional
  auto *I32Ty = Type::getInt32Ty(F->getContext());
  auto *threshold = ConstantInt::get(I32Ty, 500);
  auto *cmp = Builder.CreateICmpUGT(n_zeros, threshold, "cmp");
  auto *br = Builder.CreateCondBr(cmp, C->getLoopPreheader(), L->getLoopPreheader());
  pp->getTerminator()->eraseFromParent();
}

/// \brief Clones the original loop \p OrigLoop structure
/// and keeps it ready to add the basic blocks.
static void create_new_loops(Loop *OrigLoop,
                             LoopInfo *LI,
                             Loop *ParentLoop,
                             std::map<Loop *, Loop *> &ClonedLoopMap) {
  if (OrigLoop->empty())
    return;

  for (auto CurrLoop : OrigLoop->getSubLoops()) {
    Loop *NewLoop = LI->AllocateLoop();
    ParentLoop->addChildLoop(NewLoop);
    ClonedLoopMap[CurrLoop] = NewLoop;

    // Recursively add the new loops.
    create_new_loops(CurrLoop, LI, NewLoop, ClonedLoopMap);
  }
}

static void fix_loop_branches(Loop *ClonedLoop, BasicBlock *pre, ValueToValueMapTy &VMap) {
  // errs() << "cloned: " << *ClonedLoop << "\n";

  SWAP_SUCCESSOR(pre, VMap);

  for (auto *BB : ClonedLoop->blocks()) {
    SWAP_SUCCESSOR(BB, VMap);
  }

  for (auto L : ClonedLoop->getSubLoops()) {
    fix_loop_branches(L, L->getLoopPreheader(), VMap);
  }
}

/// \brief Clones a loop \p OrigLoop.  Returns the loop and the blocks in \p
/// Blocks.
///
/// Updates LoopInfo and DominatorTree assuming the loop is dominated by block
/// \p LoopDomBB.  Insert the new blocks before block specified in \p Before.
static Loop *clone_loop_with_preheader(BasicBlock *Before,
                                       BasicBlock *LoopDomBB,
                                       Loop *OrigLoop,
                                       ValueToValueMapTy &VMap,
                                       const Twine &NameSuffix,
                                       LoopInfo *LI,
                                       DominatorTree *DT,
                                       SmallVectorImpl<BasicBlock *> &Blocks) {
  Function *F = OrigLoop->getHeader()->getParent();
  Loop *ParentLoop = OrigLoop->getParentLoop();

  Loop *NewLoop = LI->AllocateLoop();
  if (ParentLoop)
    ParentLoop->addChildLoop(NewLoop);
  else
    LI->addTopLevelLoop(NewLoop);

  // Map each old Loop with new one.
  std::map<Loop *, Loop *> ClonedLoopMap;
  // Add the top level loop provided for cloning.
  ClonedLoopMap[OrigLoop] = NewLoop;

  // Recursively clone the loop structure.
  create_new_loops(OrigLoop, LI, NewLoop, ClonedLoopMap);

  BasicBlock *OrigPH = OrigLoop->getLoopPreheader();
  assert(OrigPH && "No preheader");
  BasicBlock *NewPH = deep_clone(OrigPH, VMap, NameSuffix, F);
  // To rename the loop PHIs.
  VMap[OrigPH] = NewPH;
  Blocks.push_back(NewPH);

  // Update LoopInfo.
  if (ParentLoop)
    ParentLoop->addBasicBlockToLoop(NewPH, *LI);

  // Update DominatorTree.
  DT->addNewBlock(NewPH, LoopDomBB);

  for (BasicBlock *BB : OrigLoop->getBlocks()) {
    BasicBlock *NewBB = CloneBasicBlock(BB, VMap, NameSuffix, F);
    VMap[BB] = NewBB;

    // Get the innermost loop for the BB.
    Loop *L = LI->getLoopFor(BB);
    // Get the corresponding cloned loop.
    Loop *NewClonedLoop = ClonedLoopMap[L];
    assert(NewClonedLoop && "Could not find the corresponding cloned loop");
    // Update LoopInfo.
    NewClonedLoop->addBasicBlockToLoop(NewBB, *LI);

    // Add DominatorTree node. After seeing all blocks, update to correct IDom.
    DT->addNewBlock(NewBB, NewPH);

    Blocks.push_back(NewBB);
  }

  for (BasicBlock *BB : OrigLoop->getBlocks()) {
    // Update DominatorTree.
    BasicBlock *IDomBB = DT->getNode(BB)->getIDom()->getBlock();
    DT->changeImmediateDominator(cast<BasicBlock>(VMap[BB]), cast<BasicBlock>(VMap[IDomBB]));
  }

  for (BasicBlock *BB : NewLoop->blocks()) {
    remap_nodes(BB, VMap);
  }

  fix_loop_branches(NewLoop, NewPH, VMap);

  return NewLoop;
}

// Clone the function
static Function *clone_function(Function *F, ValueToValueMapTy &VMap) {
  Function *clone = phoenix::CloneFunction(F, VMap);
  clone->setLinkage(Function::AvailableExternallyLinkage);

  return clone;
}

static void slice_function(ProgramSlicing *PS, Function *clone, Instruction *target) {
  PS->slice(clone, target);
}

// slice, add counter, return value
static Instruction *create_counter(Function *C) {
  auto *I32Ty = Type::getInt32Ty(C->getContext());

  IRBuilder<> Builder(C->getEntryBlock().getFirstNonPHI());
  Instruction *ptr = Builder.CreateAlloca(I32Ty, nullptr, "ptr");
  Builder.CreateStore(ConstantInt::get(I32Ty, 0), ptr);

  return ptr;
}

static void increment_counter(Function *C, Instruction *target, Instruction *ptr, Value *constant) {
  IRBuilder<> Builder(target->getNextNode());

  auto *I32Ty = Type::getInt32Ty(C->getContext());
  auto *zero = ConstantInt::get(I32Ty, 0);
  auto *one = ConstantInt::get(I32Ty, 1);

  LoadInst *counter = Builder.CreateLoad(ptr, "counter");

  Value *cmp;
  if (target->getType()->isFloatingPointTy())
    cmp = Builder.CreateFCmpOEQ(target, constant, "cmp");
  else
    cmp = Builder.CreateICmpEQ(target, constant, "cmp");

  Value *select = Builder.CreateSelect(cmp, zero, one);
  Value *inc = Builder.CreateAdd(counter, select, "inc");
  Builder.CreateStore(inc, ptr);
}

static void change_return(Function *C, Instruction *ptr) {
  for (auto &BB : *C) {
    if (ReturnInst *ri = dyn_cast<ReturnInst>(BB.getTerminator())) {
      IRBuilder<> Builder(ri);
      Instruction *counter = Builder.CreateLoad(ptr, "counter");
      Builder.CreateRet(counter);

      ri->dropAllReferences();
      ri->eraseFromParent();
    }
  }
}

static void add_counter(Function *C, Instruction *target, Value *constant) {
  Instruction *ptr = create_counter(C);
  increment_counter(C, target, ptr, constant);
  change_return(C, ptr);
}

void manual_profile(Function *F,
                    LoopInfo *LI,
                    DominatorTree *DT,
                    PostDominatorTree *PDT,
                    ProgramSlicing *PS,
                    // the set of nodes that are in the same loop
                    std::vector<ReachableNodes> loop_reachables){
  // for (auto &target_node : s) {
  //   ValueToValueMapTy VMap;

  //   Function *clone = clone_function(F, VMap);
  //   Instruction *target = cast<Instruction>(VMap[target_node->getInst()]);
  //   slice_function(PS, clone, target);
  //   add_counter(clone, target, target_node->getConstant());
  //   clone->viewCFG();
  // }

  // Store the loops already processed
  //  - Key is a pointer to the (original) most outer loop.
  //  - Value is a structure that saves the necessary info to perform the
  //    optimization later on
  // static LoopOptPropertiesMap processed_loops;

  // auto &target_node = *s.begin();
  // Loop *L = get_outer_loop(LI, target_node->getInst()->getParent());

  // if (processed_loops.find(L) == processed_loops.end()) {
  //   // needs to clone the loop
  //   auto *st = new LoopOptProperties();

  //   // pp -> ph -> h
  //   auto *ph = L->getLoopPreheader();
  //   auto *h = L->getHeader();
  //   auto *pp = split_pre_header(L, LI, DT);
  //   SmallVector<BasicBlock *, 32> Blocks;
  //   Loop *C = phoenix::clone_loop_with_preheader(pp, ph, L, st->VMap, ".c", LI, DT, Blocks);
  //   fill_control(F, pp, L, C, LI, DT);

  //   st->entry = pp;
  //   st->orig = L;
  //   st->clone = C;

  //   processed_loops[L] = st;
  // }

  // auto *st = processed_loops[L];

  // StoreInst *store = cast<StoreInst>(st->VMap[g.get_store_inst()]);
  // for (auto &node : s) {
  //   Value *V = node->getValue();
  //   Value *cnt = node->getConstant();
  //   insert_if(store, st->VMap[V], cnt);
  // }
}


void manual_profile(Function *F,
                    LoopInfo *LI,
                    DominatorTree *DT,
                    PostDominatorTree *PDT,
                    ProgramSlicing *PS,
                    std::vector<ReachableNodes> &reachables){

  std::map<Loop*, std::vector<ReachableNodes>> mapa;
  for (ReachableNodes &r : reachables){
    BasicBlock *BB = r.get_store()->getParent();
    Loop *L = LI->getLoopFor(BB);
    mapa[L].push_back(r);
    // NodeSet nodes = r.get_nodeset();
    // manual_profile(F, LI, DT, PDT, PS, r.get_store(), nodes);
  }

  for (auto kv : mapa){
    errs() << "#stores in the same loop: " << kv.second.size() << "\n";
  }
}

}  // namespace phoenix