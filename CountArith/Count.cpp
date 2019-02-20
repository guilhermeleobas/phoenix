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

void Count::dump() {

  std::vector<Instruction*> vec;
  for (auto &it : mapa){
    vec.push_back (it.first);
  }

  sort(begin(vec), end(vec), [](const Instruction *a, const Instruction *b){
    return a->getOpcode() > b->getOpcode();
  });

  for (Instruction *I : vec) {
    unsigned id = mapa[I];
    const DebugLoc &loc = I->getDebugLoc();

    if (loc) {
      auto *Scope = cast<DIScope>(loc.getScope());
      errs() << "Assigning " << id << " to " << *I << " ["
             << Scope->getFilename() << ":" << loc.getLine() << "]"
             << "\n";
    } else
      errs() << "Assigning " << id << " to " << *I << "\n";
  }

  printf("\n");
  // errs() << "\n";
}

unsigned Count::get_id(Instruction *I){
  if (mapa.find(I) == mapa.end()){
    assign_id(I);
  }
  assert(mapa.find(I) != mapa.end());
  return mapa[I];
}

void Count::assign_id(Instruction *I){
  assert(mapa.find(I) == mapa.end() && "Instructino already exists on map<Instruction*, unsigned>");
  unsigned opcode = I->getOpcode();
  mapa[I] = ++opcode_counter[opcode];;
}

void Count::track_int(Module &M, Instruction *I, Value *op1, Value *op2,
                      Geps &g) {

  IRBuilder<> Builder(g.get_store_inst()); // Store is the insertion point

  Constant *const_function = M.getOrInsertFunction(
      "record_arith_int", FunctionType::getVoidTy(M.getContext()),
      Type::getInt64Ty(M.getContext()),   // opcode
      Type::getInt64Ty(M.getContext()),   // ID
      Type::getInt64Ty(M.getContext()),   // op1
      Type::getInt64Ty(M.getContext()),   // op2
      Type::getInt8PtrTy(M.getContext()), // dest_address
      Type::getInt8PtrTy(M.getContext()), // op_address
      Type::getInt32Ty(M.getContext()));  // operand_pos

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
  params.push_back(
      Builder.CreateBitCast(g.get_dest_gep(), Builder.getInt8PtrTy())); // destiny GEP
  params.push_back(
      Builder.CreateBitCast(g.get_op_gep(), Builder.getInt8PtrTy())); // destiny GEP
  params.push_back(Builder.getInt32(g.get_operand_pos())); // Operand pos (1 or 2)
  CallInst *call = Builder.CreateCall(f, params);
}

void Count::track_float(Module &M, Instruction *I, Value *op1, Value *op2,
                        Geps &g) {
  assert(op1->getType()->isFloatingPointTy());
  assert(op2->getType()->isFloatingPointTy());

  IRBuilder<> Builder(g.get_store_inst()); // Store is the insertion point

  Constant *const_function = M.getOrInsertFunction(
      "record_arith_float", FunctionType::getVoidTy(M.getContext()),
      Type::getInt64Ty(M.getContext()),   // opcode
      Type::getInt64Ty(M.getContext()),   // ID
      Type::getDoubleTy(M.getContext()),  // op1
      Type::getDoubleTy(M.getContext()),  // op2
      Type::getInt8PtrTy(M.getContext()), // dest_address
      Type::getInt8PtrTy(M.getContext()), // op_address
      Type::getInt32Ty(M.getContext()));  // operand_pos

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
  params.push_back(
      Builder.CreateBitCast(g.get_dest_gep(), Builder.getInt8PtrTy())); // destiny GEP
  params.push_back(
      Builder.CreateBitCast(g.get_op_gep(), Builder.getInt8PtrTy())); // destiny GEP
  params.push_back(Builder.getInt32(g.get_operand_pos())); // Operand pos (1 or 2)
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

  for (auto &F : M) {
    if (F.isDeclaration() || F.isIntrinsic() ||
        F.hasAvailableExternallyLinkage())
      continue;

    Identify *Idn = &getAnalysis<Identify>(F);

    llvm::SmallVector<Geps,10> gs = Idn->get_instructions_of_interest();

    // Let's give an id for each instruction of interest
    for (auto &g : gs) {
      Instruction *I = g.get_instruction();
      // errs() << "I: " << *I << "\n";
      assign_id(I);

      // sanity check for vector instructions
      if (I->getOperand(0)->getType()->isVectorTy() ||
          I->getOperand(1)->getType()->isVectorTy())
        assert(0 && "Vector type");

      if (I->getType()->isFloatingPointTy()) {
        track_float(M, I, I->getOperand(0), I->getOperand(1), g);
      } else {
        track_int(M, I, I->getOperand(0), I->getOperand(1), g);
      }
    }
  }

  return false;
}

void Count::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<Identify>();
  AU.setPreservesAll();
}

char Count::ID = 0;
static RegisterPass<Count> X("CountArith", "Count pattern a = a OP b");
