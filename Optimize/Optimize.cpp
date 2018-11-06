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
#include "llvm/Support/raw_ostream.h" // For dbgs()
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include "Optimize.h"

#define DEBUG_TYPE "Optimize"

bool Optimize::runOnFunction(Function &F) {

  if (F.isDeclaration() || F.isIntrinsic() || F.hasAvailableExternallyLinkage())
    return true;

  Identify *Idn = &getAnalysis<Identify>();

  std::vector<Geps> gs = Idn->get_instructions_of_interest();

  // Let's give an id for each instruction of interest
  for (auto &g : gs) {
    Instruction *I = g.I;

    // sanity check for vector instructions
    if (I->getOperand(0)->getType()->isVectorTy() ||
        I->getOperand(1)->getType()->isVectorTy())
      assert(0 && "Vector type");

    errs() << *I << "\n";
  }

  return false;
}

void Optimize::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<Identify>();
  AU.setPreservesAll();
}

char Optimize::ID = 0;
static RegisterPass<Optimize> X("Optimize", "Optimize pattern a = a OP b");
