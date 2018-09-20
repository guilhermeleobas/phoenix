#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Debug.h"        // To print error messages.
#include "llvm/ADT/Statistic.h"        // For the STATISTIC macro.
#include "llvm/IR/InstIterator.h"      // To use the iterator instructions(f)
#include "llvm/IR/Instructions.h"      // To have access to the Instructions.
#include "llvm/IR/Constants.h"         // For ConstantData, for instance.
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Support/raw_ostream.h"  // For dbgs()
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/IR/DebugInfoMetadata.h" // For DILocation
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/IntrinsicInst.h"

#include "Count.h"

#define DEBUG_TYPE "Count"

#include <map>



void Count::print_instructions(Module &M){
  for (auto &F : M){
    errs() << F.getName() << "\n";
    for (auto &BB : F){
      for (auto &I : BB){
        errs() << '\t' << I << "\n";
      }
    }
  }
}

Value* Count::getElementPtr(Value* V, std::set<Value*> *s){
  
  s->insert(V);
  
  if (GetElementPtrInst *ge = dyn_cast<GetElementPtrInst>(V)){
    errs() << "Returning: " << *ge << "\n";
    return ge;
  }
  
  if (Instruction *ins = dyn_cast<Instruction>(V)){
    for (auto &op : ins->operands()){
      if (s->find(op) == s->end()){
        errs() << "\tValue: " << *op << "\n";
        Value *v = getElementPtr(op, s);
        if (v != nullptr)
          return v;
      }
    }
  }
  
  return nullptr;
}


bool Count::runOnModule(Module &M) {
  
  for (auto &F : M){
    for (auto &BB : F){
      for (auto &I : BB){
        
        if (StoreInst *store = dyn_cast<StoreInst>(&I)){
          std::set<Value*> s;
          errs() << "[Inst]: " << *store << "\n";
          Value *v = store->getOperand(0);
          Value *ptr = store->getOperand(1);
          
          auto gep_v = getElementPtr(v, &s);
          s.clear();
          auto gep_ptr = getElementPtr(ptr, &s);
          
          if (gep_v == gep_ptr){
            errs() << *gep_v << " -=-=-=- " << *gep_ptr << "\n";
          }
          
        }
      }
    }
  }

  return true;
}

char Count::ID = 0;
static RegisterPass<Count> X("Count",
    "Count pattern a = a OP b");
