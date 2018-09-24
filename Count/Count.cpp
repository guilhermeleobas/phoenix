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
#include "llvm/Support/Debug.h"
#include "llvm/IR/IntrinsicInst.h"

#include <iostream>
#include <fstream>

#include "Count.h"

#define DEBUG_TYPE "Count"

#include <map>

// use a table to store previous results (memoize)
// recognize a few more identities
// memorySSA


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

Value* Count::getElementPtr(Value* V, std::set<Value*> *s, int depth=1){
  
  s->insert(V);
  
  if (GetElementPtrInst *ge = dyn_cast<GetElementPtrInst>(V)){
    DEBUG(dbgs() << "Returning: " << *ge << "\n");
    return ge;
  }
  
  if (Instruction *ins = dyn_cast<Instruction>(V)){
    for (auto &op : ins->operands()){
      if (s->find(op) == s->end()){
        
        for (int i=0; i<depth; i++)
          DEBUG(dbgs() << "\t");
        DEBUG(dbgs() << "[Value]: " << *op << "\n");
        
        Value *v = getElementPtr(op, s, depth+1);
        if (v != nullptr)
          return v;
      }
    }
  }
  
  return nullptr;
}


bool Count::runOnFunction(Function &F) {
  
  for (auto &BB : F){
    for (auto &I : BB){
      
      if (StoreInst *store = dyn_cast<StoreInst>(&I)){
        
        store_count++;
        
        std::set<Value*> s;
        DEBUG(dbgs() << "[Inst]: " << *store << "\n");
        Value *v = store->getOperand(0);
        Value *ptr = store->getOperand(1);
        
        DEBUG(dbgs() << " [Check]: " << *v << "\n");
        auto gep_v = getElementPtr(v, &s);
        s.clear();
        DEBUG(dbgs() << " [Check]: " << *ptr << "\n");
        auto gep_ptr = getElementPtr(ptr, &s);

        if (gep_v != nullptr and gep_ptr != nullptr and gep_v == gep_ptr){
          eq_count++;
          DEBUG(dbgs() << "Equals" << *gep_v << " -=-=-=- " << *gep_ptr << "\n");
        }
        
      }
    }
  }
  
  std::ofstream myfile;
  myfile.open ("store_count.txt");
  myfile << eq_count << ", " << store_count << "\n";
  myfile.close();
  return true;
}

char Count::ID = 0;
static RegisterPass<Count> X("Count",
    "Count pattern a = a OP b");
