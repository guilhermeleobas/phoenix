#include "llvm/ADT/Statistic.h"  // For the STATISTIC macro.
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/IR/Constants.h"          // For ConstantData, for instance.
#include "llvm/IR/DebugInfoMetadata.h"  // For DILocation
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"  // To use the iterator instructions(f)
#include "llvm/IR/Instructions.h"  // To have access to the Instructions.
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"  // To print error messages.
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"  // For dbgs()
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include <fstream>
#include <iostream>
#include <set>
#include <stack>

using std::set;
using std::stack;

#include "Store.h"

#define DEBUG_TYPE "StoreCount"

unsigned Store::get_id(StoreInst *S) {
  if (mapa.find(S) == mapa.end()) {
    assign_id(S);
  }
  assert(mapa.find(S) != mapa.end());
  return mapa[S];
}

void Store::assign_id(StoreInst *S) {
  assert(mapa.find(S) == mapa.end() && "Instruction already exists on map<Instruction*, unsigned>");
  mapa[S] = mapa.size();
}

template<class T>
Value *get_constantint(Module *M, T num) {
  auto *I64Ty = Type::getInt64Ty(M->getContext());
  return ConstantInt::get(I64Ty, num);
}


void Store::create_call(Module *M,
                        StoreInst *S,
                        const StringRef &function_name,
                        Value *store_id,
                        Value *is_marked,
                        Value *cmp) {
  IRBuilder<> Builder(S);

  auto *I64Ty = Type::getInt64Ty(M->getContext());
  auto *I1Ty = Type::getInt1Ty(M->getContext());

  Constant *const_function =
      M->getOrInsertFunction(function_name, FunctionType::getVoidTy(M->getContext()),
                             I64Ty,   // store_id
                             I64Ty,   // is_marked
                             I64Ty);  // cmp

  Function *f = cast<Function>(const_function);

  std::vector<Value *> params;
  params.push_back(store_id);
  params.push_back(is_marked);
  params.push_back(cmp);
  CallInst *call = Builder.CreateCall(f, params);
}

void Store::track_store(Module *M, StoreInst *S, unsigned store_id, bool is_marked) {
  IRBuilder<> Builder(S);

  Value *ptr = S->getPointerOperand();
  Value *val = S->getValueOperand();

  LoadInst *load = Builder.CreateLoad(ptr, "load");

  Value *cmp;
  if (val->getType()->isFloatingPointTy()) {
    cmp = Builder.CreateFCmpOEQ(val, load);
  } else {
    cmp = Builder.CreateICmpEQ(val, load);
  }

  Value *store_id_value = get_constantint(M, store_id);
  Value *is_marked_value = get_constantint(M, is_marked);
  Value *cmp_value = Builder.CreateZExt(cmp, Builder.getInt64Ty());

  create_call(M, S, "record_store", store_id_value, is_marked_value, cmp_value);
}

// Create a call to `dump_records` function
void Store::insert_dump_call(Module *M, Instruction *I) {
  IRBuilder<> Builder(I);

  // Let's create the function call
  Constant *const_function =
      M->getOrInsertFunction("dump_records", FunctionType::getVoidTy(M->getContext()));

  Function *f = cast<Function>(const_function);

  // Create the call
  Builder.CreateCall(f, std::vector<Value *>());
}

// Find the return inst and create call to `dump_records`
void Store::insert_dump_call(Module *M) {
  for (auto &F : *M) {
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

bool Store::runOnModule(Module &M) {
  insert_dump_call(&M);

  for (auto &F : M) {
    if (F.isDeclaration() || F.isIntrinsic() || F.hasAvailableExternallyLinkage())
      continue;

    Identify *Idn = &getAnalysis<Identify>(F);

    llvm::SmallVector<Geps, 10> gs = Idn->get_instructions_of_interest();

    // Let's give an id for each instruction of interest
    for (auto &g : gs) {
      StoreInst *S = g.get_store_inst();
      marked_stores.insert(S);
      // assign_id(S);
    }
  }

  for (auto &F : M){
    for (Instruction &I : instructions(F)){
      if (StoreInst *S = dyn_cast<StoreInst>(&I)){
        bool marked = (marked_stores.find(S) != marked_stores.end()) ? true : false;
        unsigned store_id = get_id(S);
        track_store(&M, S, store_id, marked);
      }
    }
  }

  return false;
}

void Store::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<Identify>();
  AU.setPreservesAll();
}

char Store::ID = 0;
static RegisterPass<Store> X("CountStores", "Count silent stores");
