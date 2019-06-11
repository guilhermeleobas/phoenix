#include "ProgramSlicing.h"

#include "../PDG/PDGAnalysisWrapperPass.h"
#include "llvm/Analysis/LoopInfo.h"

#include "llvm/CodeGen/UnreachableBlockElim.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Transforms/Scalar/DCE.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/LoopUtils.h"

#include <iterator>
#include <queue>
#include <set>

namespace phoenix {

void DetatchDeadBlocks(
     ArrayRef<BasicBlock *> BBs,
     SmallVectorImpl<DominatorTree::UpdateType> *Updates,
     bool KeepOneInputPHIs) {
   for (auto *BB : BBs) {
     // Loop through all of our successors and make sure they know that one
     // of their predecessors is going away.
     SmallPtrSet<BasicBlock *, 4> UniqueSuccessors;
     for (BasicBlock *Succ : successors(BB)) {
       Succ->removePredecessor(BB, KeepOneInputPHIs);
       if (Updates && UniqueSuccessors.insert(Succ).second)
         Updates->push_back({DominatorTree::Delete, BB, Succ});
     }
 
     // Zap all the instructions in the block.
     while (!BB->empty()) {
       Instruction &I = BB->back();
       // If this instruction is used, replace uses with an arbitrary value.
       // Because control flow can't get here, we don't care what we replace the
       // value with.  Note that since this block is unreachable, and all values
       // contained within it must dominate their uses, that all uses will
       // eventually be removed (they are themselves dead).
       if (!I.use_empty())
         I.replaceAllUsesWith(UndefValue::get(I.getType()));
       BB->getInstList().pop_back();
     }
     new UnreachableInst(BB->getContext(), BB);
     assert(BB->getInstList().size() == 1 &&
            isa<UnreachableInst>(BB->getTerminator()) &&
            "The successor list of BB isn't empty before "
            "applying corresponding DTU updates.");
   }
 }

void DeleteDeadBlocks(ArrayRef<BasicBlock *> BBs, bool KeepOneInputPHIs) {
#ifndef NDEBUG
  // Make sure that all predecessors of each dead block is also dead.
  SmallPtrSet<BasicBlock *, 4> Dead(BBs.begin(), BBs.end());
  assert(Dead.size() == BBs.size() && "Duplicating blocks?");
  for (auto *BB : Dead)
    for (BasicBlock *Pred : predecessors(BB))
      assert(Dead.count(Pred) && "All predecessors must be dead!");
#endif

  SmallVector<DominatorTree::UpdateType, 4> Updates;
  DetatchDeadBlocks(BBs, nullptr, KeepOneInputPHIs);


  for (BasicBlock *BB : BBs)
    BB->eraseFromParent();
}

bool EliminateUnreachableBlocks(Function &F, bool KeepOneInputPHIs=false) {
  df_iterator_default_set<BasicBlock *> Reachable;

  // Mark all reachable blocks.
  for (BasicBlock *BB : depth_first_ext(&F, Reachable))
    (void)BB /* Mark all reachable blocks */;

  // Collect all dead blocks.
  std::vector<BasicBlock *> DeadBlocks;
  for (Function::iterator I = F.begin(), E = F.end(); I != E; ++I)
    if (!Reachable.count(&*I)) {
      BasicBlock *BB = &*I;
      DeadBlocks.push_back(BB);
    }

  // Delete the dead blocks.
  DeleteDeadBlocks(DeadBlocks, KeepOneInputPHIs);

  return !DeadBlocks.empty();
}

void ProgramSlicing::set_entry_block(Loop *L) {
  // BasicBlock *entry = BasicBlock::Create(F->getContext(), "function_entry", F, preheader);
  // IRBuilder<> Builder(entry);
  // Builder.CreateBr(preheader);

  BasicBlock *preheader = L->getLoopPreheader();
  BasicBlock *entry = &F->getEntryBlock();
  if (entry == preheader)
    return;

  BasicBlock *pred = preheader->getUniquePredecessor();
  preheader->removePredecessor(pred);

  // BranchInst *pred_br = cast<BranchInst>(pred->getTerminator());
  // for (unsigned i = 0; i < pred_br->getNumSuccessors(); i++) {
  //   if (pred_br->getSuccessor(i) == preheader) {
  //     pred_br->setSuccessor(i, pred);
  //   }
  // }

  BranchInst *br = cast<BranchInst>(entry->getTerminator());
  br->setSuccessor(0, preheader);
}

void ProgramSlicing::set_exit_block(Loop *L) {
  BasicBlock *L_exit = L->getExitBlock();
  BasicBlock *exit = BasicBlock::Create(F->getContext(), "function_exit", F, L_exit);

  IRBuilder<> Builder(exit);
  Builder.CreateRetVoid();

  // set the jump to exit block
  // errs() << "terminator: " << *L_exit->getTerminator() << "\n";
  if (BranchInst *br = dyn_cast<BranchInst>(L_exit->getTerminator())){
    br->setSuccessor(0, exit);
  }
}

Loop *ProgramSlicing::remove_loops_outside_chain(Loop *L, Loop *keep) {
  if (L == nullptr)
    return keep;

  // the idea is:
  // for each subloop \in L, connect the subloop pre-header to its exit!
  // but skip the subloop *keep*

  // errs() << *L << "\n";

  for (Loop *sub : L->getSubLoops()) {
    if (sub == keep) {
      continue;
    }

    auto *pre = sub->getLoopPreheader();
    auto *exit = sub->getExitBlock();

    BranchInst *br = cast<BranchInst>(pre->getTerminator());
    // exit->getUniqueSuccessor()->removePredecessor(exit);
    br->setSuccessor(0, exit);
  }

  return remove_loops_outside_chain(L->getParentLoop(), L);
}

Loop *ProgramSlicing::remove_loops_outside_chain(BasicBlock *BB) {
  Loop *L = LI->getLoopFor(BB);
  if (L == nullptr)
    return nullptr;

  Loop *outer = remove_loops_outside_chain(L, nullptr);

  return outer;
}

// Returns the number of users of a given instruction
unsigned get_num_users(Instruction *I) {
  unsigned nUsers = 0;

  for (User *u : I->users())
    ++nUsers;

  return nUsers;
}

void ProgramSlicing::slice(Instruction *I) {
  errs() << "function name: " << F->getName() << "\n";
  errs() << "init: " << *I << "\n";

  Loop *L = remove_loops_outside_chain(I->getParent());
  errs() << "Loop: " << *L << "\n";

  set_entry_block(L);
  set_exit_block(L);

  EliminateUnreachableBlocks(*F);

  PassBuilder PB;
  FunctionAnalysisManager FAM;
  PB.registerFunctionAnalyses(FAM);

  UnreachableBlockElimPass u;
  u.run(*F, FAM);

  DCEPass d;
  d.run(*F, FAM);

  PDG->compute_dependences(F);
  std::set<Instruction *> dependences = PDG->get_all_dependences(I);

  // for (Instruction *I : dependences) {
  //   errs() << "[dep]: " << *I << "\n";
  // }

  std::queue<Instruction *> q;
  for (Instruction &I : instructions(F)) {
    if (isa<BranchInst>(&I) || isa<ReturnInst>(&I) || dependences.find(&I) != dependences.end())
      continue;

    I.dropAllReferences();
    q.push(&I);
    // I.eraseFromParent();
  }

  while (!q.empty()) {
    Instruction *I = q.front();
    q.pop();
    I->eraseFromParent();
  }

  // F->viewCFG();
}

ProgramSlicing *ProgramSlicingWrapperPass::getPS() {
  return PS;
}

bool ProgramSlicingWrapperPass::runOnFunction(Function &F) {
  if (F.isDeclaration() || F.isIntrinsic() || F.hasAvailableExternallyLinkage())
    return false;

  auto *LI = &getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  auto *DT = &getAnalysis<DominatorTreeWrapperPass>().getDomTree();
  auto *PDT = &getAnalysis<PostDominatorTreeWrapperPass>().getPostDomTree();
  auto *PDG = getAnalysis<PDGAnalysisWrapperPass>().getPDG();

  this->PS = new ProgramSlicing(&F, LI, DT, PDT, PDG);

  // we modify the function => return true
  return true;
}

void ProgramSlicingWrapperPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<LoopInfoWrapperPass>();
  AU.addRequired<DominatorTreeWrapperPass>();
  AU.addRequired<PostDominatorTreeWrapperPass>();
  AU.addRequired<PDGAnalysisWrapperPass>();
  AU.setPreservesAll();
}

char ProgramSlicingWrapperPass::ID = 0;
static RegisterPass<ProgramSlicingWrapperPass> X(
    "PS",
    "Performs a program slicing on a program for a given entry");

};  // namespace phoenix