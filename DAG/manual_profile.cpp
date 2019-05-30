#include "llvm/ADT/Statistic.h" // For the STATISTIC macro.
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/IR/Constants.h"         // For ConstantData, for instance.
#include "llvm/IR/DebugInfoMetadata.h" // For DILocation
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h" // To use the iterator instructions(f)
#include "llvm/IR/Instructions.h" // To have access to the Instructions.
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h" // To print error messages.
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h" // For dbgs()
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include <queue>

#include "manual_profile.h"
#include "utils.h"

using namespace llvm;

#define DUMP(F, node) \
  errs() << "[" << F->getName() << "] " << *node << "\n"

#define SWAP_SUCCESSOR(BB, VMAP)                       \
do {                                                   \
  auto *br = BB->getTerminator();                      \
  for (unsigned i=0; i<br->getNumSuccessors(); ++i){   \
    auto *suc = br->getSuccessor(i);                   \
    if (VMap[suc])                                     \
    br->setSuccessor(i, cast<BasicBlock>(VMap[suc]));  \
  }                                                    \
} while (0)                                            \

namespace phoenix{

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

static std::vector<Loop*> find_loop_chain(LoopInfo *LI, Loop *L){
  std::vector<Loop*> v;

  while (true){
    unsigned depth = L->getLoopDepth();

    if (depth <= 1){
      v.push_back(L);
      break;
    }

  }
}

static std::vector<Loop*> find_loop_chain(LoopInfo *LI, BasicBlock *BB){
  return find_loop_chain(LI, LI->getLoopFor(BB));
}

static BasicBlock *split_pre_header(Loop *L, LoopInfo *LI, DominatorTree *DT) {
  // Get the loop pre-header
  auto *ph = L->getLoopPreheader();
  // split the block and move every inst to @ph, except for the branch inst
  Instruction *SplitPtr = &*ph->begin();
  auto *pp = llvm::SplitBlock(ph, SplitPtr, DT, LI);
  std::swap(pp, ph);
  for (Instruction &I : *pp){
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
static void fill_control(Function *F, BasicBlock *pp, Loop *L, Loop *C, LoopInfo *LI, DominatorTree *DT) {
  Module *M = pp->getModule();
  Function *fn = M->getFunction("__sampling");

  llvm::SmallVector<Value*, 2> args;
  for (Argument &arg : F->args())
    args.push_back(&arg);

  IRBuilder<> Builder (pp->getFirstNonPHI());
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
static void create_new_loops(Loop *OrigLoop, LoopInfo *LI, Loop *ParentLoop,
   std::map<Loop*, Loop*>  &ClonedLoopMap) {
  if (OrigLoop->empty()) return;

  for (auto CurrLoop :  OrigLoop->getSubLoops()) {
    Loop *NewLoop = LI->AllocateLoop();
    ParentLoop->addChildLoop(NewLoop);
    ClonedLoopMap[CurrLoop] = NewLoop;

    // Recursively add the new loops.
    create_new_loops(CurrLoop, LI, NewLoop, ClonedLoopMap);
  }
}

static void print(Loop *L){
  errs() << "pre header: " << *L->getLoopPreheader() << "\n";
  for (auto *BB : L->getBlocks()){
    errs() << *BB << "\n";
  }
  errs() << "\n";
}

static void fix_loop_branches(Loop *ClonedLoop, BasicBlock *pre, ValueToValueMapTy &VMap){
  // errs() << "cloned: " << *ClonedLoop << "\n";

  SWAP_SUCCESSOR(pre, VMap);

  for (auto *BB : ClonedLoop->blocks()){
    SWAP_SUCCESSOR(BB, VMap);
  }

  for (auto L : ClonedLoop->getSubLoops()){
    fix_loop_branches(L, L->getLoopPreheader(), VMap);
  }
}

/// \brief Clones a loop \p OrigLoop.  Returns the loop and the blocks in \p
/// Blocks.
///
/// Updates LoopInfo and DominatorTree assuming the loop is dominated by block
/// \p LoopDomBB.  Insert the new blocks before block specified in \p Before.
static Loop *clone_loop_with_preheader(BasicBlock *Before, BasicBlock *LoopDomBB,
                                   Loop *OrigLoop, ValueToValueMapTy &VMap,
                                   const Twine &NameSuffix, LoopInfo *LI,
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
  std::map<Loop*, Loop*> ClonedLoopMap;
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
    Loop* L = LI->getLoopFor(BB);
    // Get the corresponding cloned loop.
    Loop* NewClonedLoop = ClonedLoopMap[L];
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
    DT->changeImmediateDominator(cast<BasicBlock>(VMap[BB]),
                                 cast<BasicBlock>(VMap[IDomBB]));
  }

  for (BasicBlock *BB : NewLoop->blocks()){
    remap_nodes(BB, VMap);
  }

  // Move them physically from the end of the block list.
  // F->getBasicBlockList().splice(Before->getIterator(), F->getBasicBlockList(),
  //                               NewPH);
  // F->getBasicBlockList().splice(Before->getIterator(), F->getBasicBlockList(),
  //                               NewLoop->getHeader()->getIterator(), F->end());

  fix_loop_branches(NewLoop, NewPH, VMap);

  return NewLoop;
}


/// \brief Given a Value *V, find all the matrices and arrays that builds *V and returns
/// its types
static std::vector<Type*> get_function_types(Node *node){
  std::queue<Node*> q;
  std::vector<Type*> T;

  q.push(node);

  while (!q.empty()){
    Node *N = q.front();
    q.pop();

    errs() << "Node: " << *N << "\n";

    if (LoadNode *p = dyn_cast<LoadNode>(N)){
      LoadInst *load = dyn_cast<LoadInst>(p->getInst());
      T.push_back (load->getPointerOperandType());
    }
    else if (BinaryNode *b = dyn_cast<BinaryNode>(N)){
      q.push(b->left);
      q.push(b->right);
    }
    else if (UnaryNode *u = dyn_cast<UnaryNode>(N)){
      q.push(u->child);
    }
    else {
      errs() << "!pattern match: " << *node << "\n";
      continue;
    }
  }

  return T;
}

/// \brief Given a Loop *L and an Instruction *I, this method creates a new function @F
/// with a copy of *L inside of it to keep track of how many times *I == 0.0
/// 
/// Optimizations: Given that one can one keep track of the values of *I during
/// iterations. One can remove all the variables from @F whose *I does not have a
/// direct depende
static Function* create_function(Loop *L, Node *node){
  // auto types = getFunctionType(node);
  // Instruction *I = node->getInst();

  // FunctionType *FT = FunctionType::get(Type::getInt32Ty(getGlobalContext()), types, false);

  // Function *F = Function::Create(FT, Function::ExternalLinkage, Name, TheModule);
}

/// Clones @L and remove every instruction that is not part of the profilling process
/// An instruction is said to be part of the profilling iff its value is necessary to
/// compute another inst in the nodeset @s
///
/// to-do: move the loop to a function
static void create_profile_loop(Loop *L, LoopInfo *LI, DominatorTree *DT,
                                  NodeSet &s){

}

void manual_profile(Function *F, LoopInfo *LI, DominatorTree *DT, PostDominatorTree *PDT, 
                    const Geps &g, NodeSet &s) {

  // Store the loops already processed
  //  - Key is a pointer to the (original) most outer loop. 
  //  - Value is a structure that saves the necessary info to perform the
  //    optimization later on
  // 
  // As a static member of the method, this variable will persist
  // over calls
  static LoopOptPropertiesMap processed_loops; 

  auto &first_node = *s.begin();
  Loop *L = get_outer_loop(LI, first_node->getInst()->getParent());

  // if (processed_loops.find(L) == processed_loops.end()){
  //   DUMP(F, first_node);
  //   // needs to clone the loop

  //   auto *st = new LoopOptProperties();

  //   // pp -> ph -> h
  //   auto *ph = L->getLoopPreheader();
  //   auto *h = L->getHeader();
  //   auto *pp = split_pre_header(L, LI, DT);
  //   SmallVector<BasicBlock*, 32> Blocks;
  //   Loop *C = phoenix::clone_loop_with_preheader(pp, ph, L, st->VMap, ".c", LI, DT, Blocks);
  //   fill_control(F, pp, L, C, LI, DT);

  //   st->pp = pp;
  //   st->L = L;
  //   st->C = C;

  //   processed_loops[L] = st;
  // }

  // auto *st = processed_loops[L];

  // StoreInst *store = cast<StoreInst>(st->VMap[g.get_store_inst()]);
  // for (auto &node : s){

    // auto x = get_function_types(node);

    // Value *V = node->getValue();
    // Value *cnt = node->getConstant();
    // insert_if(store, st->VMap[V], cnt);
  // }
}

} // namespace phoenix