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

#include <fstream>
#include <iostream>
#include <map>
#include <stack>

#include "Count.h"

#define DEBUG_TYPE "Count"

void Count::print_instructions(Module &M) {
  for (auto &F : M) {
    errs() << F.getName() << "\n";
    for (auto &BB : F) {
      for (auto &I : BB) {
        errs() << '\t' << I << "\n";
      }
    }
  }
}

bool Count::is_arith_inst(Instruction *I) {
  switch (I->getOpcode()) {
  case Instruction::Add:
  case Instruction::Sub:
  case Instruction::Mul:
  case Instruction::UDiv:
  case Instruction::SDiv:
  case Instruction::Shl:
  case Instruction::LShr:
  case Instruction::AShr:
  case Instruction::And:
  case Instruction::Or:
  case Instruction::Xor:
    return true;
  default:
    return false;
  }
}

void Count::track(Module &M, Instruction *I, Value *op1, Value *op2) {
  assert(is_arith_inst(I));

  IRBuilder<> Builder(I);

  Constant *const_function = M.getOrInsertFunction(
      "record_arith", FunctionType::getVoidTy(M.getContext()),
      Type::getInt64Ty(M.getContext()),    // opcode
      Type::getInt64Ty(M.getContext()),    // op1
      Type::getInt64Ty(M.getContext()));   // op2

  Function *f = cast<Function>(const_function);

  Type *i64 = Builder.getInt64Ty();
  Value *se1 = Builder.CreateSExt(op1, i64);
  Value *se2 = Builder.CreateSExt(op2, i64);

  // Create the call
  std::vector<Value *> params;
  params.push_back(Builder.getInt64(I->getOpcode())); // opcode
  params.push_back(se1); // value1
  params.push_back(se2);
  CallInst *call = Builder.CreateCall(f, params);
}


// Create a call to `dump_arith` function
void Count::insert_dump_call(Module &M, Instruction *I) {
  IRBuilder<> Builder(I);

  // Let's create the function call
  Constant *const_function = M.getOrInsertFunction(
      "dump_arith", FunctionType::getVoidTy(M.getContext()));

  Function *f = cast<Function>(const_function);

  // Create the call
  Builder.CreateCall(f, std::vector<Value *>());
}


// Find the return inst and create call to `dump_arith`
void Count::insert_dump_call(Module &M) {
  for (auto &F : M) {
    for (auto &BB : F) {
      for (auto &I : BB) {
        if (ReturnInst *ri = dyn_cast<ReturnInst>(&I)) {
          if (F.getName() == "main")
            insert_dump_call(M, ri);
        } else if (CallInst *ci = dyn_cast<CallInst>(&I)) {
          Function *fun = ci->getCalledFunction();
          if (fun && fun->getName() == "exit") {
            insert_dump_call(M, ci);
          }
        }
      }
    }
  }
}

bool Count::runOnModule(Module &M) {

  insert_dump_call(M); 

  for (auto &F : M){
    for (auto &BB : F){
      for (auto &I : BB){
        if (is_arith_inst(&I)){
          track(M, &I, I.getOperand(0), I.getOperand(1));
        }
      }
    }
  }

  return false;
}

void Count::getAnalysisUsage(AnalysisUsage &AU) const { AU.setPreservesAll(); }

char Count::ID = 0;
static RegisterPass<Count> X("CountArith", "Count pattern a = a OP b");
