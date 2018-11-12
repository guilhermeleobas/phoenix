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

#include "Optimize.h"

#define DEBUG_TYPE "Optimize"

Value *Optimize::get_identity(const Instruction *I) {
  switch (I->getOpcode()) {
  case Instruction::Add:
  case Instruction::Sub:
  case Instruction::Xor:
    return ConstantInt::get(I->getType(), 0);
  case Instruction::Mul:
    return ConstantInt::get(I->getType(), 1);
  case Instruction::FAdd:
  case Instruction::FSub:
    return ConstantFP::get(I->getType(), 0.0);
  case Instruction::FMul:
    return ConstantInt::get(I->getType(), 1.0);
  // case Instruction::UDiv:
  // case Instruction::SDiv:
  // case Instruction::Shl:
  // case Instruction::LShr:
  // case Instruction::AShr:
  // case Instruction::And:
  // case Instruction::Or:
  default:
    std::string str = "Instruction not supported: ";
    llvm::raw_string_ostream rso(str);
    I->print(rso);
    llvm_unreachable(str.c_str());
  }
}

void Optimize::insert_if(const Geps &g) {
  Instruction *v = g.get_v_as_inst();
  Instruction *I = g.get_instruction();
  StoreInst *store = g.get_store_inst();
  IRBuilder<> Builder(v->getNextNode());

  Value *idnt;
  Value *cmp;

  if (v->getType()->isFloatingPointTy()) {
    cmp = Builder.CreateFCmpONE(v, get_identity(I));
  } else {
    cmp = Builder.CreateICmpNE(v, get_identity(I));
  }

  TerminatorInst *br = llvm::SplitBlockAndInsertIfThen(
      cmp, dyn_cast<Instruction>(cmp)->getNextNode(), false);

  BasicBlock *prev = store->getParent();

  while(&*prev->begin() != store){
    Instruction *i = &*prev->begin();
    i->moveBefore(br);
  }

  store->moveBefore(br);

  DEBUG(errs() << *br->getParent() << "\n");

  // for (BasicBlock::iterator i = prev->begin(), e = prev->end(); i != e; ++i) {
  //   DEBUG(dbgs() << "inst: " << *i << ' ' << "\n");
  //   if (&(*i) == store) {
  //     break;
  //   }
  //   i->removeFromParent();
  //   i->insertBefore(br);
  // }

  // DEBUG(dbgs() << "I: " << *I << "\n");
  // DEBUG(dbgs() << "v: " << *v << "\n");
  // DEBUG(dbgs() << "cmp: " << *cmp << "\n");
  // DEBUG(dbgs() << "Branch: " << *br << "\n");
}

bool Optimize::runOnFunction(Function &F) {

  if (F.isDeclaration() || F.isIntrinsic() || F.hasAvailableExternallyLinkage())
    return true;

  Identify *Idn = &getAnalysis<Identify>();

  std::vector<Geps> gs = Idn->get_instructions_of_interest();

  // Let's give an id for each instruction of interest
  for (auto &g : gs) {
    Instruction *I = g.get_instruction();

    // sanity check for vector instructions
    if (I->getOperand(0)->getType()->isVectorTy() ||
        I->getOperand(1)->getType()->isVectorTy())
      assert(0 && "Vector type");

    insert_if(g);

    errs() << "foi\n";
  }

  return false;
}

void Optimize::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<Identify>();
  AU.setPreservesAll();
}

char Optimize::ID = 0;
static RegisterPass<Optimize> X("Optimize", "Optimize pattern a = a OP b");
