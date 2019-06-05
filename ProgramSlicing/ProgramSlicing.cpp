#include "ProgramSlicing.h"

#include "../PDG/PDGAnalysisWrapperPass.h"
#include "llvm/Analysis/LoopInfo.h"

#include "llvm/Transforms/Scalar/DCE.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/LoopUtils.h"

namespace phoenix {

/// Remove the loops that aren't part of the chain of loops
/// that @V is a part of

void ProgramSlicing::remove_loops_outside_chain(Loop *L, Loop *keep){

  if (L == nullptr)
    return;

  // the idea is:
  // for each subloop \in L, connect the subloop pre-header to its exit!
  // but skip the subloop *keep*

  errs() << *L << "\n";

  for (Loop *sub : L->getSubLoops()){
    if (sub == keep) 
      continue;

    auto *pre = sub->getLoopPreheader();
    auto *exit = sub->getExitBlock();

    BranchInst *br = cast<BranchInst>(pre->getTerminator());
    // exit->getUniqueSuccessor()->removePredecessor(exit);
    br->setSuccessor(0, exit);
    errs() << "branch: " << *br << "\n";
  }

  remove_loops_outside_chain(L->getParentLoop(), L);
}

void ProgramSlicing::remove_loops_outside_chain(BasicBlock *BB) {
  Loop *L = LI->getLoopFor(BB);
  if (L == nullptr)
    return;

  remove_loops_outside_chain(L, nullptr);
}

void ProgramSlicing::slice(Instruction *I) {
  remove_loops_outside_chain(I->getParent());

  PDG->compute_dependences(I->getParent()->getParent());
  std::set<Instruction*> s = PDG->get_all_dependences(I);

  errs() << "start: " << *I << "\n";
  for (auto *v : s){
    errs() << *v << "\n";
  }
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

  this->PS = new ProgramSlicing(LI, DT, PDT, PDG);

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