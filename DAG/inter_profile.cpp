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
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/Debug.h"  // To print error messages.
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"  // For dbgs()
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/LoopUtils.h"

#include <queue>

#include "../ProgramSlicing/ProgramSlicing.h"
#include "inter_profile.h"
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
  std::vector<Instruction *> insts;
  for (Instruction &I : *pp) {
    if (isa<TerminatorInst>(&I))
      break;
    insts.push_back(&I);
  }

  for (Instruction *I : insts) {
    I->moveBefore(&*ph->begin());
  }
  // pp is the pre preHeader
  return pp;
}

static void dump_ret_value(Module *M, IRBuilder<> &Builder, Value *v) {
  // Let's create the function call
  Function *f = M->getFunction("dump");
  std::vector<Value *> params;

  // Create the call
  auto *I32Ty = Type::getInt32Ty(M->getContext());
  Value *V = Builder.CreateSExt(v, I32Ty);
  params.push_back(V);
  Builder.CreateCall(f, params);
}

// Creates a call to each sampling function
// aggregates all its values and decide wether or not jump to the
// optimized loop
//  - @F : The function
//  - @pp : The loop pre preheader
//  - @L/@C : Original/Cloned loops
static void create_controller(Function *F,
                              std::map<Instruction *, Function *> samplings,
                              BasicBlock *pp,
                              Loop *L,
                              Loop *C) {
  IRBuilder<> Builder(pp->getFirstNonPHI());

  // aggregate the return of each sampling call
  std::vector<CallInst *> calls;

  for (auto kv : samplings) {
    Function *fn_sampling = kv.second;

    llvm::SmallVector<Value *, 2> args;
    for (Argument &arg : F->args())
      args.push_back(&arg);

    CallInst *call = Builder.CreateCall(fn_sampling, args, "call", nullptr);
    calls.push_back(call);
  }

  Value *cmp = calls[0];

  for (unsigned i = 1; i < calls.size(); i++) {
    cmp = Builder.CreateOr(cmp, calls[i]);
    // cmp = Builder.CreateAnd(cmp, calls[i]);
  }

  auto *br = Builder.CreateCondBr(cmp, C->getLoopPreheader(), L->getLoopPreheader());
  // add_dump_msg(C->getLoopPreheader(), "going to clone\n");
  // add_dump_msg(C->getLoopPreheader(), F->getName());
  // add_dump_msg(L->getLoopPreheader(), "going to original loop\n");
  // add_dump_msg(L->getLoopPreheader(), F->getName());
  // add_dump_msg(L->getLoopPreheader(), " -- function original loop\n");
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
static Function *clone_function(Function *F, ValueToValueMapTy &VMap, const Twine &name) {
  Function *clone = phoenix::CloneFunction(F, VMap, name);
  clone->setLinkage(Function::PrivateLinkage);

  return clone;
}

static void slice_function(Function *clone, Instruction *target) {
  ProgramSlicing *PS;
  PS->slice(clone, target);

  // ensure that the sliced function will not introduce any side effect
  for (Instruction &I : instructions(*clone)) {
    assert(!isa<StoreInst>(I) && "sampling function has a store instruction");

    for (unsigned i = 0; i < I.getNumOperands(); i++) {
      assert(!isa<UndefValue>(I.getOperand(i)) && "operand is undef");
    }
    // To-do: whitelist of functions without side effect (i.e. sqrt)
    /* assert(!isa<CallInst>(I) && "sampling function has a call instruction"); */
  }
}

// slice, add counter, return value
static Instruction *create_counter(Function *C, const Twine &name) {
  auto *I32Ty = Type::getInt32Ty(C->getContext());

  IRBuilder<> Builder(C->getEntryBlock().getFirstNonPHI());
  Instruction *ptr = Builder.CreateAlloca(I32Ty, nullptr, name);
  Builder.CreateStore(ConstantInt::get(I32Ty, 0), ptr);

  return ptr;
}

static void increment_cnt_counter(Function *C, Instruction *value_after, Instruction *ptr) {
  IRBuilder<> Builder(value_after->getNextNode());

  auto *I32Ty = Type::getInt32Ty(C->getContext());
  auto *one = ConstantInt::get(I32Ty, 1);

  LoadInst *counter = Builder.CreateLoad(ptr, "cnt");
  Value *inc = Builder.CreateAdd(counter, one, "cnt_inc");
  Builder.CreateStore(inc, ptr);
}

static void increment_eq_counter(Function *C,
                                 Instruction *value_before,
                                 Instruction *value_after,
                                 Instruction *ptr) {
  IRBuilder<> Builder(value_after->getNextNode());

  auto *I32Ty = Type::getInt32Ty(C->getContext());
  auto *zero = ConstantInt::get(I32Ty, 0);
  auto *one = ConstantInt::get(I32Ty, 1);

  LoadInst *counter = Builder.CreateLoad(ptr, "eq_counter");

  Value *cmp;

  if (value_after->getType()->isFloatingPointTy()) {
    // Value *sub = Builder.CreateFSub(value_before, value_after);

    // add_dump_msg(value_after, "sub: %.10f\n", sub);

    // Module *M = C->getParent();
    // Function *abs = get_abs(M, value_after->getType());
    // std::vector<Value*> params;
    // params.push_back(sub);
    // CallInst *value = Builder.CreateCall(abs, params);
    // Constant *eps = ConstantFP::get(value_before->getType(), 20.0);
    // cmp = Builder.CreateFCmpOLT(value_before, eps, "fcmp");
    cmp = Builder.CreateFCmpOEQ(value_before, value_after, "fcmp");
  } else {
    cmp = Builder.CreateICmpEQ(value_before, value_after, "cmp");
  }

  // add_dump_msg(value_after, "----BEGIN----\n");
  // add_dump_msg(value_before, "before: %.3lf\n", value_before);
  // add_dump_msg(value_after, "after: %.3lf\n", value_after);
  // add_dump_msg(value_after, "cmp: %d\n", cmp);
  // add_dump_msg(value_after, "----END----\n");

  Value *select = Builder.CreateSelect(cmp, one, zero);
  Value *inc = Builder.CreateAdd(counter, select, "eq_inc");
  Builder.CreateStore(inc, ptr);
}

static void change_return(Function *C, Instruction *eq_ptr, Instruction *cnt_ptr, Value *treshold) {
  auto *I1Ty = Type::getInt1Ty(C->getContext());
  auto *zero = ConstantInt::get(I1Ty, 0);
  auto *one = ConstantInt::get(I1Ty, 1);

  for (auto &BB : *C) {
    if (ReturnInst *ri = dyn_cast<ReturnInst>(BB.getTerminator())) {
      IRBuilder<> Builder(ri);

      // cnt is the number of times the sampling function was executed
      // the default value is 1000
      Instruction *cnt = Builder.CreateLoad(cnt_ptr, "cnt");
      Instruction *eq = Builder.CreateLoad(eq_ptr, "eq");

      // now we need to compare if the eq is SMALLER than a threshold
      // if eq ~= cnt, then the store was silent most of the times
      // therefore we return 1
      Value *sub = Builder.CreateSub(cnt, eq, "sub");

      // add_dump_msg(&BB, "function: ");
      // add_dump_msg(&BB, C->getName());
      // add_dump_msg(&BB, "\n");
      // add_dump_msg(&BB, "cnt value: %d\n", cnt);
      // add_dump_msg(&BB, "eq value: %d\n", eq);
      // add_dump_msg(&BB, "sub value: %d\n", sub);

      Value *cmp = Builder.CreateICmpSLE(sub, treshold, "cmp");
      Value *ret = Builder.CreateSelect(cmp, one, zero);

      Builder.CreateRet(ret);

      ri->dropAllReferences();
      ri->eraseFromParent();
    }
  }
}

static BranchInst *get_branch_inst(BasicBlock *BB) {
  Instruction *term = BB->getTerminator();
  assert(isa<BranchInst>(term) && "term is not a branch inst");
  BranchInst *br = cast<BranchInst>(term);
  return br;
}

static Value *get_constantint(Function *F, int num) {
  auto *I32Ty = Type::getInt32Ty(F->getContext());
  return ConstantInt::get(I32Ty, num);
}

static Value *get_constantint(Type *T, int num) {
  return ConstantInt::get(T, num);
}

static BasicBlock *find_exit_block(Function *F) {
  for (Instruction &I : instructions(F))
    if (isa<ReturnInst>(&I))
      return I.getParent();

  return BasicBlock::Create(F->getContext(), "exit_block", F);

  llvm_unreachable("could not find exit block!");
}

static void limit_num_iter(Function *fn,
                           Instruction *entry_point,
                           Instruction *cnt_ptr,
                           Value *num_iter) {
  BasicBlock *exit = find_exit_block(fn);
  BasicBlock *body = entry_point->getParent();
  BasicBlock *prox = body->getUniqueSuccessor();

  BasicBlock *split = SplitBlock(body, body->getTerminator());

  Instruction *term = split->getTerminator();
  term->dropAllReferences();
  term->eraseFromParent();

  // Loads the counter
  // compare it against num_iter
  IRBuilder<> Builder(split);
  LoadInst *cnt = Builder.CreateLoad(cnt_ptr);
  Value *cond = Builder.CreateICmpSLT(cnt, num_iter);
  Builder.CreateCondBr(cond, prox, exit);
}

static void add_counters(Function *C, Instruction *value_before, Instruction *value_after) {
  Instruction *eq_ptr = create_counter(C, "eq");
  Instruction *cnt_ptr = create_counter(C, "cnt");

  increment_eq_counter(C, value_before, value_after, eq_ptr);
  increment_cnt_counter(C, value_after, cnt_ptr);

#define N_ITER 1000
#define GAP ((N_ITER / 2) + 1)
  change_return(C, eq_ptr, cnt_ptr, get_constantint(C, GAP));
  limit_num_iter(C, value_after, cnt_ptr, get_constantint(C, N_ITER));
}

static bool getIncomingAndBackEdge(Loop *L, BasicBlock *&Incoming, BasicBlock *&Backedge) {
  BasicBlock *H = L->getHeader();

  Incoming = nullptr;
  Backedge = nullptr;
  pred_iterator PI = pred_begin(H);
  assert(PI != pred_end(H) && "Loop must have at least one backedge!");
  Backedge = *PI++;
  if (PI == pred_end(H))
    return false;  // dead loop
  Incoming = *PI++;
  if (PI != pred_end(H))
    return false;  // multiple backedges?

  if (L->contains(Incoming)) {
    if (L->contains(Backedge))
      return false;
    std::swap(Incoming, Backedge);
  } else if (!L->contains(Backedge))
    return false;

  assert(Incoming && Backedge && "expected non-null incoming and backedges");
  return true;
}

static Instruction *get_predicate(BasicBlock *BB) {
  Value *term = BB->getTerminator();
  assert(isa<BranchInst>(term) && "BB TerminatorInst is not a branch inst");
  BranchInst *br = cast<BranchInst>(term);
  if (br->isConditional())
    return cast<Instruction>(br->getCondition());
  else
    return get_predicate(BB->getSingleSuccessor());
}

static PHINode *getInductionVariable(Loop *L) {
  BasicBlock *H = L->getHeader();

  BasicBlock *Incoming = nullptr, *Backedge = nullptr;
  if (!getIncomingAndBackEdge(L, Incoming, Backedge))
    return nullptr;

  // Loop over all of the PHI nodes, looking for a canonical indvar.
  for (BasicBlock::iterator I = H->begin(); isa<PHINode>(I); ++I) {
    PHINode *PN = cast<PHINode>(I);
    Value *iv = PN->getIncomingValueForBlock(Incoming);
    if (isa<ConstantInt>(iv) || isa<PHINode>(iv))
      if (Instruction *Inc = dyn_cast<Instruction>(PN->getIncomingValueForBlock(Backedge)))
        if ((Inc->getOpcode() == Instruction::Add && Inc->getOperand(0) == PN) ||
            (Inc->getOpcode() == Instruction::Sub && Inc->getOperand(0) == PN))
          if (ConstantInt *CI = dyn_cast<ConstantInt>(Inc->getOperand(1)))
            return PN;
  }

  errs() << "returning nullptr\n";
  return nullptr;
}

static CallInst *create_call_to_rand(Function *C, IRBuilder<> &Builder) {
  Module *M = C->getParent();
  Function *rand = get_rand(M);
  std::vector<Value *> params;
  return Builder.CreateCall(rand, params, "rand");
}

static void change_loop_range(Function *C, Loop *L) {
  errs() << "[INFO]: changing loop range for " << C->getName() << "\n";
  PHINode *iv = getInductionVariable(L);
  assert(iv && "iv == nullptr");
  errs() << "induction variable: " << *iv << "\n";

  Type *I64Ty = IntegerType::getInt64Ty(C->getContext());

  Instruction *pred = get_predicate(iv->getParent());
  Value *array_size = pred->getOperand(1);

  // replace the predicate
  if (CmpInst *cmp = dyn_cast<CmpInst>(pred)) {
    if (L->getParentLoop() == nullptr)
      cmp->setPredicate(CmpInst::Predicate::ICMP_NE);
    else
      cmp->setPredicate(CmpInst::Predicate::ICMP_SLT);
  }

  // add_dump_msg(pred, "pred: %d\n", pred);
  // add_dump_msg(pred, "first operand: %d\n", pred->getOperand(0));
  // add_dump_msg(pred, "second operand: %d\n", pred->getOperand(1));

  // iv = PHI [a, %BB1], [I, %BB2], ...
  for (unsigned i = 0; i < iv->getNumOperands(); i++) {
    BasicBlock *incoming = iv->getIncomingBlock(i);
    if (incoming == L->getLoopLatch()) {
      Instruction *Inc = cast<Instruction>(iv->getOperand(i));
      errs() << "Inst: " << *Inc << "\n";
      // we know now that the *I is the increment instruction
      for (int j = 0; j < Inc->getNumOperands(); j++) {
        if (isa<ConstantInt>(Inc->getOperand(j))) {
          IRBuilder<> Builder(Inc);

          CallInst *call = create_call_to_rand(C, Builder);
          Value *sext = Builder.CreateSExt(call, I64Ty);

          if (!array_size->getType()->isIntegerTy(64))
            array_size = Builder.CreateSExt(array_size, I64Ty);
          Value *rem = Builder.CreateSRem(sext, array_size, "rem");

          Inc->setOperand(j, rem);

          // in the outermost loop, we always take the array_size of the final value
          if (L->getParentLoop() == nullptr) {
            Builder.SetInsertPoint(Inc->getNextNode());
            Value *final_rem = Builder.CreateSRem(Inc, array_size, "finalrem");
            // replaces the uses of I with final_rem
            Inc->replaceAllUsesWith(final_rem);
            cast<Instruction>(final_rem)->setOperand(0, Inc);
          }

          // Deals with the case that the size of the loop (@array_size) and
          // the loop increment (@I) are defined in the same basic block:
          if (Instruction *modI = dyn_cast<Instruction>(array_size)) {
            BasicBlock *BB = modI->getParent();
            if (BB == Inc->getParent() and distance(BB, modI) > distance(BB, Inc)) {
              modI->moveBefore(BB->getFirstNonPHI());
            }
          }

          // add_dump_msg(Inc, "----\n");
          // add_dump_msg(Inc, "var name: ");
          // add_dump_msg(Inc, Inc->getName());
          // add_dump_msg(Inc, "\nvar value: %d\n", Inc->getOperand(j == 0 ? 1 : 0));

          // add_dump_msg(Inc, "Increment value by: %d\n", rem);
          // add_dump_msg(Inc, "mod: %d\n", array_size);
          // add_dump_msg(Inc, "final value: %d\n", Inc);
        }
      }

      // in some cases, the increment is in the same basic block of the predicate
      // we just add another check to prevent the increment to be greater than
      // the array size
      if (iv->getParent() == Inc->getParent()) {
        IRBuilder<> Builder(pred->getNextNode());
        Value *cond = Builder.CreateICmpSLT(Inc, array_size);
        Value *new_pred = Builder.CreateAnd(cond, pred);
        BranchInst *br = cast<BranchInst>(iv->getParent()->getTerminator());
        br->setCondition(new_pred);
      }
    }
  }
}

static void change_ranges(Function *C, Instruction *entry_point) {
  DominatorTree DT(*C);
  LoopInfo LI(DT);

  Loop *L = LI.getLoopFor(entry_point->getParent());

  while (true) {
    if (!L)
      break;
    change_loop_range(C, L);
    L = L->getParentLoop();
  }
}

static void inter_profilling(Function *F,
                             LoopInfo *LI,
                             DominatorTree *DT,
                             // the set of stores that are in the same loop chain
                             std::vector<ReachableNodes> &stores_in_loop,
                             unsigned num_stores) {
  // Now, create the sampling functions
  //
  // InstToFnSamplingMap maps the arithmetic instruction to a sampling function
  std::map<Instruction *, Function *> InstToFnSamplingMap;

  errs() << "Function: " << F->getName() << "\n";

  for (ReachableNodes &rn : stores_in_loop) {
    errs() << "[START]: "
           << "store: " << *rn.get_store() << "\n";
    ValueToValueMapTy Fn_VMap;
    Function *fn_sampling = clone_function(F, Fn_VMap, "sampling");

    Instruction *arith = rn.get_arith_inst();
    LoadInst *load = rn.get_load();

    Instruction *value_before = cast<Instruction>(Fn_VMap[load]);
    Instruction *value_after = cast<Instruction>(Fn_VMap[arith]);

    errs() << "slicing on: " << *value_after << "\n";

    slice_function(fn_sampling, value_after);
    change_ranges(fn_sampling, value_after);
    add_counters(fn_sampling, value_before, value_after);

    InstToFnSamplingMap[arith] = fn_sampling;

    errs() << "[END]: " << fn_sampling->getName() << "\n\n";
  }

  // Clone the loop;
  ValueToValueMapTy Loop_VMap;

  Loop *orig_loop = get_outer_loop(LI, stores_in_loop[0].get_store()->getParent());
  BasicBlock *ph = orig_loop->getLoopPreheader();
  BasicBlock *h = orig_loop->getHeader();
  BasicBlock *pp = split_pre_header(orig_loop, LI, DT);

  SmallVector<BasicBlock *, 32> Blocks;
  Loop *cloned_loop =
      phoenix::clone_loop_with_preheader(pp, ph, orig_loop, Loop_VMap, ".c", LI, DT, Blocks);

  create_controller(F, InstToFnSamplingMap, pp, orig_loop, cloned_loop);

  // Optimize the cloned loop
  for (ReachableNodes &rn : stores_in_loop) {
    StoreInst *store = cast<StoreInst>(Loop_VMap[rn.get_store()]);
    for (phoenix::Node *node : rn.get_nodeset()) {
      Value *V = cast<Value>(Loop_VMap[node->getValue()]);
      Value *constant = node->getConstant();
      insert_if(store, V, constant);
    }
  }
}

void inter_profilling(Function *F,
                      LoopInfo *LI,
                      DominatorTree *DT,
                      std::vector<ReachableNodes> &reachables) {
  std::map<Loop *, std::vector<ReachableNodes>> mapa;

  if (reachables.empty())
    return;

  // First we map stores in the same outer loop into a Map
  for (ReachableNodes &r : reachables) {
    BasicBlock *BB = r.get_store()->getParent();
    Loop *L = get_outer_loop(LI, BB);
    mapa[L].push_back(r);
  }

  // Then, we create a copy of the outer loop alongside each sampling function
  for (auto kv : mapa) {
    DT->recalculate(*F);
    inter_profilling(F, LI, DT, kv.second, kv.second.size());
  }
}

}  // namespace phoenix
