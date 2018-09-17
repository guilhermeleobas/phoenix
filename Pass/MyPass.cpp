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
#include "llvm/IR/Metadata.h"
#include "llvm/IR/IRBuilder.h"

#include "MyPass.h"

#define DEBUG_TYPE "MyPass"

bool MyPass::runOnModule(Module &M) {

  for (auto &F : M){
    for (auto &BB : F){
      for (auto &I : BB){
        errs() << I << "\n"; 
      }
    }
  }

  return true;
}

char MyPass::ID = 0;
static RegisterPass<MyPass> X("MyLLVMPass",
    "My LLVM Pass description");
