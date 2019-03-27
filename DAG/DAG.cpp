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

#include "DAG.h"
#include "dotVisitor.h"
#include "constraintVisitor.h"

#define DEBUG_TYPE "DAG"

bool DAG::runOnFunction(Function &F) {

  if (F.isDeclaration() || F.isIntrinsic() || F.hasAvailableExternallyLinkage())
    return true;

  Identify *Idn = &getAnalysis<Identify>();

  llvm::SmallVector<Geps, 10> gs = Idn->get_instructions_of_interest();

  // Let's give an id for each instruction of interest
  for (auto &g : gs) {
    Instruction *I = g.get_instruction();

    // sanity check for vector instructions
    if (I->getOperand(0)->getType()->isVectorTy() ||
        I->getOperand(1)->getType()->isVectorTy()){
      continue;
      // errs() << *I << "\n";
      // assert(0 && "Vector type");
    }

    phoenix::StoreNode *store = cast<phoenix::StoreNode>(myParser(g.get_store_inst()));

    DotVisitor t;
    ConstraintVisitor cv(store);

    store->accept(cv);
    store->accept(t);

    t.print();

    // dumpExpression(node);
    // dumpDot(node);
    // errs() << "Height: " << node->getHeight() << "\n";
    // static_cast<phoenix::StoreNode*>(node)->dumpDebugInfo();
    // errs() << "\n";
  }

  return false;
}

void DAG::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<Identify>();
  AU.addRequired<LoopInfoWrapperPass>();
  AU.setPreservesAll();
}

char DAG::ID = 0;
static RegisterPass<DAG> X("DAG", "DAG pattern a = a OP b", false, false);
