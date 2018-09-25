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
#include "llvm/Analysis/MemorySSA.h"
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
#include <map>
#include <stack>

#include "Count.h"

#define DEBUG_TYPE "Count"


std::map<Value*, Value*> mapa;

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
  
  if (mapa.find(V) != mapa.end())
    return mapa[V];
  
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

MemoryUse* Count::getMemoryUse(const MemorySSA &mssa, const MemoryDef *def, Instruction *I){
  std::stack<Instruction*> s;
  std::set<Instruction*> visited;
  s.push(I);
  
  while (!s.empty()){
    Instruction *I = s.top();
    s.pop();
    if (I == nullptr)
      continue;
    
    visited.insert(I);
    
    // dbgs() << "[Inst]: " << *I << "\n";
    
    MemoryUseOrDef *info = mssa.getMemoryAccess(I);
    
    if (info == nullptr){
      for (auto &op : I->operands()){
        Instruction *ins = dyn_cast<Instruction>(op);
        if (visited.find(ins) == visited.end())
          s.push(ins);
      }
    }
    else if(MemoryUse *use = dyn_cast<MemoryUse>(info)) {
      return use;
    }
    
  }
  
  return nullptr;
}


bool Count::runOnFunction(Function &F) {
  
  // for (auto &BB : F){
  //   for (auto &I : BB){
  //     
  //     if (StoreInst *store = dyn_cast<StoreInst>(&I)){
  //
  //       store_count++;
  //       
  //       std::set<Value*> s;
  //       DEBUG(dbgs() << "[Inst]: " << *store << "\n");
  //       Value *v = store->getOperand(0);
  //       Value *ptr = store->getOperand(1);
  //       
  //       DEBUG(dbgs() << " [Check]: " << *v << "\n");
  //       auto gep_v = getElementPtr(v, &s);
  //       s.clear();
  //       DEBUG(dbgs() << " [Check]: " << *ptr << "\n");
  //       auto gep_ptr = getElementPtr(ptr, &s);
  //       
  //       mapa[v] = gep_v;
  //       mapa[ptr]= gep_ptr;
  //
  //       if (gep_v != nullptr and gep_ptr != nullptr and gep_v == gep_ptr){
  //         eq_count++;
  //         DEBUG(dbgs() << "Equals" << *gep_v << " -=-=-=- " << *gep_ptr << "\n");
  //       }
  //       
  //     }
  //   }
  // }
  
	MemorySSA &mssa = getAnalysis<MemorySSAWrapperPass>().getMSSA();  
  
  errs() << "[Function]: " << F.getName() << "\n";
  for (auto &BB : F){
    for (auto &I : BB){
      if (StoreInst *store = dyn_cast<StoreInst>(&I)){
        store_count++;
          
        // MemoryDef* s = mssa.getMemoryAccess(store);
        MemoryDef *s = dyn_cast<MemoryDef>(mssa.getMemoryAccess(store));
        MemoryUse *r = getMemoryUse(mssa, s, dyn_cast<Instruction>(store->getOperand(0)) );
        
        if (r != nullptr)
          errs() << "[Inst]: " << *store << "\n" << *s << ' ' << *r << ' ' << ( r->getID() ) << "\n\n";
        
      }
    }
  }
  
  DEBUG(dbgs() << eq_count << ", " << store_count << "\n");
  
  std::ofstream myfile;
  myfile.open ("store_count.txt");
  myfile << eq_count << ", " << store_count << "\n";
  myfile.close();
  return true;
}

void Count::getAnalysisUsage(AnalysisUsage &AU) const {
	AU.setPreservesAll();
	AU.addRequired<MemorySSAWrapperPass>();
}

char Count::ID = 0;
static RegisterPass<Count> X("Count",
    "Count pattern a = a OP b");
