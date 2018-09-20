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

#include "Instrument.h"

#define DEBUG_TYPE "Instrument"

#include <map>

std::map<std::string, Value*> variables;
std::map<std::string, int> group_identity;

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
 * add => a + 0 = a
 * sub => a - 0 = a
 * 
 * mul => a * 1 = a
 * div => a / 1 = a
 * 
 * and => a & a = a
 * or  => a | a = a
 * 
*/

void Instrument::init_group_identity(){
  group_identity["add"] = 0;
  group_identity["sub"] = 0;
  group_identity["mul"] = 1;
}

Value* Instrument::alloc_string(Instruction *I){

  /* 
  A variable is just a char* with some text identifying the instruction.
  For instance, we use the getOpcodeName() function.
  */
  const std::string opcodeName = I->getOpcodeName();

  // If we already create a variable for the opcodeName, let's just return it
  if (variables.find(opcodeName) != variables.end())
    return variables[opcodeName];

  // Otherwise, let's create one
  IRBuilder<> Builder(I);
  Value *var = Builder.CreateGlobalStringPtr(StringRef(opcodeName));
  variables[opcodeName] = var;

  return var;
}

Value* Instrument::alloc_counter(Module &M, Instruction *I){
  
  std::string opcodeName = I->getOpcodeName();
  
  IRBuilder<> Builder(I);

  M.getOrInsertGlobal(opcodeName + "_inc", Builder.getInt64Ty());
  
  GlobalVariable *gVar = M.getNamedGlobal(opcodeName + "_inc"); 
  return gVar;
}

void Instrument::insert_inc(Module &M, Instruction *I){

  alloc_counter(M, I);
  IRBuilder<> Builder(I);
  
  std::string opcode = I->getOpcodeName();

  GlobalVariable *gVar = M.getNamedGlobal(std::string(I->getOpcodeName()) + "_inc");
  Value *v0 = I->getOperand(0), *v1 = I->getOperand(1);
  Value *cmp;
  
  if (v1->getType()->isIntegerTy(32)){
    cmp = Builder.CreateICmpEQ(v1, Builder.getInt32(group_identity[opcode]), "compa");
  }
  else if (v1->getType()->isIntegerTy(64)) {
    cmp = Builder.CreateICmpEQ(v1, Builder.getInt64(group_identity[opcode]), "compa");
  }
  
	// TerminatorInst *t = SplitBlockAndInsertIfThen(cmp, I, false);
  
  // Value *cmp = Builder.CreateICmpEQ(v0, Builder.getInt32(0), "cmp");

  // LoadInst *Load = Builder.CreateLoad(I);

  // LoadInst *Load = Builder.CreateLoad(gVar);
  // Value *Inc = Builder.CreateAdd(Builder.getInt64(1), Load);
  // StoreInst *Store = Builder.CreateStore(Inc, gVar);
}

void Instrument::insert_dump_call(Module &M, Instruction *I){
  IRBuilder<> Builder(I);

  // Let's create the function call
  Constant *const_function = M.getOrInsertFunction("dump_csv",
    FunctionType::getVoidTy(M.getContext()));

  Function *f = cast<Function>(const_function);

  // Create the call
  Builder.CreateCall(f, std::vector<Value*>());
}

bool Instrument::runOnModule(Module &M) {
  
  init_group_identity();

	bool foi=false;

  for (auto &F : M){
    for (auto &BB : F){
      for (auto &I : BB){

				if (foi) continue;
				errs() << "Instruction: " << I << "\n";
        
        if (BinaryOperator *bin = dyn_cast<BinaryOperator>(&I)){
					errs() << "\tBinaryInst\n";
          if (group_identity.find(bin->getOpcodeName()) != group_identity.end()){
						errs() << "\t\tCalled\n";
            insert_inc(M, bin);
						// foi = true;
					}
        }
        else if (ReturnInst *ri = dyn_cast<ReturnInst>(&I)){
          if (F.getName() == "main"){
            errs() << *ri << "\n";
            insert_dump_call(M, ri);
          }
        }
        else if (CallInst *ci = dyn_cast<CallInst>(&I)){
          Function *function = ci->getCalledFunction();
          std::string function_name = function->getName();
          if (ci->getName() == "exit"){
            insert_dump_call(M, ci);
          }
        }
        
      }
    }
  }

  return true;
}

char Instrument::ID = 0;
static RegisterPass<Instrument> X("Instrument",
    "Instrument Binary Instructions");
