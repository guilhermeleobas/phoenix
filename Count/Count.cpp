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


std::map<Instruction*, bool> mapa;

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
  
  // if (mapa.find(V) != mapa.end())
  //   return mapa[V];
  
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

bool Count::getMemoryUse(const MemorySSA &mssa, const MemoryAccess *y, Instruction *I){
  
  if (mapa.find(I) != mapa.end())
    return mapa[I];
    
  
  std::stack<Instruction*> s;
  std::set<Instruction*> visited;
  s.push(I);
  
  while (!s.empty()){
    Instruction *I = s.top(); 
    s.pop();
    
    if (I == nullptr)
      continue;
    
    // Check if there is an annotation associated with I
    MemoryUseOrDef *annot = mssa.getMemoryAccess(I);
    // if (annot)
      // errs() << "[Annotation]: " << *annot << " --> " << *annot->getOperand(0) << " compare " << *y << "\n";
    if (annot and y == annot->getOperand(0)){
      // errs() << "entrou\n";
      // if there is an annotation, grab the operand 0 and compare it with y
      return true;
    }
    
    for (auto &op : I->operands()){
      Instruction *ins = dyn_cast<Instruction>(op);
      if (visited.find(ins) == visited.end()){
        visited.insert(ins);
        s.push(ins);
      }
      
    }
  }
  
  
  return false;
}


bool Count::runOnFunction(Function &F) {
  
  // An annotation of the form x = MemoryDef(y) represents a read-write access
  // to memory. In special, any store instruction defines a new MemoryDef.
	MemorySSA &mssa = getAnalysis<MemorySSAWrapperPass>().getMSSA();  
  
  DEBUG(dbgs() << "[Function]: " << F.getName() << "\n");
  for (auto &BB : F){
    for (auto &I : BB){
      if (StoreInst *store = dyn_cast<StoreInst>(&I)){
        store_count++;
        // A store instruction: `%s = store %val, %ptr` always defines a MemoryDef
        // annotation `x = MemoryDef(y)`. We are not interested in `x` but in `y = MemoryXYZ(..)`.
        // `y` can be obtained by `x->getOperand(0)`.
        // 
        // We want to check if the possible `load` that produces %val may alias with %ptr
        // To that end, we traverse the IR backwards looking for this specific load
        // to see if its annotation is of the form `MemoryUse(y)`
        MemoryDef *s = dyn_cast<MemoryDef>(mssa.getMemoryAccess(store));
        Instruction *ins = dyn_cast<Instruction>(store->getOperand(0));
        if (getMemoryUse(mssa, s->getOperand(0), ins)){
          eq_count++;
          mapa[ins] = true;
        }
        else{
          mapa[ins] = false;
        }
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
