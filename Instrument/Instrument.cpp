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

#include <fstream>

#include "Instrument.h"

#define DEBUG_TYPE "Instrument"

#include <map>

void Instrument::print_instructions(Module &M){
  for (auto &F : M){
    for (auto &BB : F){
      for (auto &I : BB){
        errs() << I << "\n";
      }
    }
  }
}

/*
 * Create (if necessary) and return a unique ID for each load/store instruction
*/
unsigned Instrument::get_id(const Value *V){
  if (IDs.find(V) == IDs.end())
    IDs[V] = IDs.size();
  return IDs[V];
}

// Just a syntax-sugar for the get_id(Value*);
unsigned Instrument::get_id(const Instruction *I){
  return get_id(dyn_cast<Value>(I));
}

/*****************************************************************************/

/*
 * Create a global counter to represent the timestamp 
*/
// void Instrument::create_counter(Module *M){
//   M->getOrInsertGlobal("timestamp", Type::getInt64Ty( M->getContext() ));
//   gv_ts = M->getNamedGlobal("timestamp");
//   // gv_ts->setLinkage(GlobalValue::CommonLinkage);
//   gv_ts->setAlignment(4);
//
//   ConstantInt* zero = ConstantInt::get(M->getContext(), APInt(64,0));
//   gv_ts->setInitializer(zero);
// }

/*****************************************************************************/

/*****************************************************************************/

/*
 * Alloc a global string pointer
*/
// void Instrument::alloc_opcode_to_string_ptr(Instruction *I, const std::string &opcode){
//   IRBuilder<> Builder(I);
//   opcode_map[opcode] = Builder.CreateGlobalStringPtr(StringRef(opcode));
// }
//
// // Return the string associate with the opcode. Valid opcodes are Load/Store
// Value* Instrument::get_opcode_string_ptr(Instruction *I){
//   const std::string opcode = I->getOpcodeName();
//   if (opcode_map.find(opcode) == opcode_map.end())
//     alloc_opcode_to_string_ptr(I, opcode);
//     
//   return opcode_map[opcode];
// }

/*****************************************************************************/

/*
 * 
*/
void Instrument::record_access(Module *M, Instruction *I, Value *ptr, const std::string &function_name){
  IRBuilder<> Builder(I);
  
  Constant *const_function = M->getOrInsertFunction(function_name,
    FunctionType::getVoidTy(M->getContext()),
    // Type::getInt8PtrTy(M->getContext()), // Type
    Type::getInt64Ty(M->getContext()),   // ID
    // Type::getInt64Ty(M->getContext()),   // Timestamp
    Type::getInt8PtrTy(M->getContext()));  // Address

  Function *f = cast<Function>(const_function);
  
  // load and increment the timestamp counter
  // LoadInst *load_ts = Builder.CreateLoad(gv_ts);
  // Value *inc = Builder.CreateAdd(load_ts, Builder.getInt64(1));
  // StoreInst *store_ts = Builder.CreateStore(inc, gv_ts);
  
  // Value *timestamp = dyn_cast<Value>(load_ts);
  
  Value *address = Builder.CreateBitCast(ptr, Type::getInt8PtrTy(M->getContext()));
  
  // Create the call
  std::vector<Value*> params;
  // params.push_back (get_opcode_string_ptr(I)); // load or store
  params.push_back(Builder.getInt64(get_id(I)));  // id
  // params.push_back(timestamp);
  params.push_back(address);                      // address
  CallInst *call = Builder.CreateCall(f, params);
  
}

/*****************************************************************************/

void Instrument::insert_dump_call(Module *M, Instruction *I){

  IRBuilder<> Builder(I);

  // Let's create the function call
  Constant *const_function = M->getOrInsertFunction("dump",
    FunctionType::getVoidTy(M->getContext()));

  Function *f = cast<Function>(const_function);

  // Create the call
  Builder.CreateCall(f, std::vector<Value*>());
  
}

void Instrument::insert_dump_call(Module *M){
  
  for (auto &F : *M){
    for (auto &BB : F){
      for (auto &I : BB){
        if (ReturnInst *ri = dyn_cast<ReturnInst>(&I)){
          insert_dump_call(M, ri);
        }
        else if (CallInst *ci = dyn_cast<CallInst>(&I)){
          Function *fun = ci->getCalledFunction();
          if (fun && fun->getName() == "exit"){
            insert_dump_call(M, ci);
          }
        }
      }
    }
  }
  
}


/*****************************************************************************/

void Instrument::init_instrumentation(Module *M){
  
  Function *F = M->getFunction("main");
  
  Instruction *ins = F->front().getTerminator();
  
  IRBuilder<> Builder(ins);
  
  Constant *const_function = M->getOrInsertFunction("init",
      FunctionType::getVoidTy(M->getContext()));
  
  Function *f = cast<Function>(const_function);
  
  CallInst *call = Builder.CreateCall(f, std::vector<Value*>());
  
}

/*****************************************************************************/

bool Instrument::runOnFunction(Function &F) {
  
  Module *M = F.getParent();
  init_instrumentation(M);
  // return true;
  
  for (auto &BB : F){
    for (auto &I : BB){
      
      if (StoreInst *store = dyn_cast<StoreInst>(&I)){
        record_access(M, store, store->getPointerOperand(), "record_store");
        // errs() << getID(store) << "\n";
      }
      else if (LoadInst *load = dyn_cast<LoadInst>(&I)){
        record_access(M, load, load->getPointerOperand(), "record_load");
        // errs() << getID(load) << "\n";
      }
      
    }
  }
  
  return true;
}

char Instrument::ID = 0;
static RegisterPass<Instrument> X("Instrument",
    "Instrument Binary Instructions");
