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
#include <set>
#include <stack>

using std::set;
using std::stack;

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

unsigned Count::get_id(map<Instruction *, unsigned> &mapa, Instruction *I) {
  if (mapa.find(I) == mapa.end()) {
    mapa[I] = mapa.size();
    // errs() << "Assigning " << mapa[I] << " to " << *I << "\n";
  }

  return mapa[I];
}

unsigned Count::get_id(Instruction *I) {

  switch (I->getOpcode()) {
  case Instruction::Add:
    return get_id(add_map, I);
  case Instruction::Sub:
    return get_id(sub_map, I);
  case Instruction::Mul:
    return get_id(mul_map, I);
  case Instruction::Xor:
    return get_id(xor_map, I);
  case Instruction::FAdd:
    return get_id(fadd_map, I);
  case Instruction::FSub:
    return get_id(fsub_map, I);
  case Instruction::FMul:
    return get_id(fmul_map, I);
  default:
    assert(0);
  }
}

bool Count::is_arith_inst_of_interest(Instruction *I) {
  switch (I->getOpcode()) {
  case Instruction::Add:
  case Instruction::Sub:
  case Instruction::Mul:
  // case Instruction::UDiv:
  // case Instruction::SDiv:
  // case Instruction::Shl:
  // case Instruction::LShr:
  // case Instruction::AShr:
  // case Instruction::And:
  // case Instruction::Or:
  case Instruction::Xor:
  case Instruction::FAdd:
  case Instruction::FSub:
  case Instruction::FMul:
    return true;
  default:
    return false;
  }
}

void Count::track_int(Module &M, Instruction *I, Value *op1, Value *op2) {
  assert(is_arith_inst_of_interest(I));

  IRBuilder<> Builder(I);

  Constant *const_function = M.getOrInsertFunction(
      "record_arith_int", FunctionType::getVoidTy(M.getContext()),
      Type::getInt64Ty(M.getContext()),  // opcode
      Type::getInt64Ty(M.getContext()),  // ID
      Type::getInt64Ty(M.getContext()),  // op1
      Type::getInt64Ty(M.getContext())); // op2

  Function *f = cast<Function>(const_function);

  Value *se1 = op1->getType()->isIntegerTy(64)
                   ? op1
                   : Builder.CreateSExt(op1, Builder.getInt64Ty());
  Value *se2 = op2->getType()->isIntegerTy(64)
                   ? op2
                   : Builder.CreateSExt(op2, Builder.getInt64Ty());

  // Create the call
  std::vector<Value *> params;
  params.push_back(Builder.getInt64(I->getOpcode())); // opcode
  params.push_back(Builder.getInt64(get_id(I)));      // ID
  params.push_back(se1);                              // value1
  params.push_back(se2);                              // value2
  CallInst *call = Builder.CreateCall(f, params);
}

void Count::track_float(Module &M, Instruction *I, Value *op1, Value *op2) {
  assert(is_arith_inst_of_interest(I));
  assert(op1->getType()->isFloatingPointTy());
  assert(op2->getType()->isFloatingPointTy());

  IRBuilder<> Builder(I);

  Constant *const_function = M.getOrInsertFunction(
      "record_arith_float", FunctionType::getVoidTy(M.getContext()),
      Type::getInt64Ty(M.getContext()),   // opcode
      Type::getInt64Ty(M.getContext()),   // ID
      Type::getDoubleTy(M.getContext()),  // op1
      Type::getDoubleTy(M.getContext())); // op2

  Function *f = cast<Function>(const_function);

  Value *se1 = op1->getType()->isDoubleTy()
                   ? op1
                   : Builder.CreateFPExt(op1, Builder.getDoubleTy());
  Value *se2 = op2->getType()->isDoubleTy()
                   ? op2
                   : Builder.CreateFPExt(op2, Builder.getDoubleTy());

  // Create the call
  std::vector<Value *> params;
  params.push_back(Builder.getInt64(I->getOpcode())); // opcode
  params.push_back(Builder.getInt64(get_id(I)));      // ID
  params.push_back(se1);                              // value1
  params.push_back(se2);                              // value2
  CallInst *call = Builder.CreateCall(f, params);
}

bool Count::can_reach_store(Instruction *I) {
  assert(is_arith_inst_of_interest(I));

  stack<Instruction *> s;
  set<Instruction *> visited;

  s.push(I);
  visited.insert(I);

  while (!s.empty()) {
    Instruction *I = s.top();
    s.pop();

    for (Value *op : I->users()) {
      if (Instruction *n = dyn_cast<Instruction>(op)) {

        if (visited.find(n) != visited.end())
          continue;

        if (isa<StoreInst>(n))
          return true;

        s.push(n);
        visited.insert(n);
      }
    }
  }

  return false;
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

  for (auto &F : M) {
    for (auto &BB : F) {
      for (auto &I : BB) {
        if (is_arith_inst_of_interest(&I) && can_reach_store(&I)) {

          if (I.getOperand(0)->getType()->isVectorTy() ||
              I.getOperand(1)->getType()->isVectorTy())
            errs() << "Vector: " << I << "\n";

          if (I.getType()->isFloatingPointTy()) {
            track_float(M, &I, I.getOperand(0), I.getOperand(1));
          } else
            track_int(M, &I, I.getOperand(0), I.getOperand(1));
        }
      }
    }
  }

  return false;
}

void Count::getAnalysisUsage(AnalysisUsage &AU) const { AU.setPreservesAll(); }

char Count::ID = 0;
static RegisterPass<Count> X("CountArith", "Count pattern a = a OP b");
