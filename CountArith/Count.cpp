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

void Count::dump(const map<Instruction *, unsigned> &mapa) {
  for (auto &it : mapa) {
    errs() << "Assigning " << it.second << " to " << *it.first << "\n";
  }
  // errs() << "\n";
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

void Count::track_int(Module &M, Instruction *I, Value *op1, Value *op2, Geps &g) {
  assert(is_arith_inst_of_interest(I));

  IRBuilder<> Builder(g.store); // Store is the insertion point

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
  params.push_back(Builder.getInt64(I->getOpcode()));                          // opcode
  params.push_back(Builder.getInt64(get_id(I)));                               // ID
  params.push_back(se1);                                                       // value1
  params.push_back(se2);                                                       // value2
  params.push_back(Builder.CreateBitCast(g.dest_gep, Builder.getInt8PtrTy())); // destiny GEP
  params.push_back(Builder.CreateBitCast(g.op_gep, Builder.getInt8PtrTy()));   // destiny GEP
  params.push_back(Builder.getInt32(g.operand_pos));                           // Operand pos (1 or 2)
  CallInst *call = Builder.CreateCall(f, params);
}

void Count::track_float(Module &M, Instruction *I, Value *op1, Value *op2, Geps &g) {
  assert(is_arith_inst_of_interest(I));
  assert(op1->getType()->isFloatingPointTy());
  assert(op2->getType()->isFloatingPointTy());

  IRBuilder<> Builder(g.store); // Store is the insertion point

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
  params.push_back(Builder.getInt64(I->getOpcode()));                          // opcode
  params.push_back(Builder.getInt64(get_id(I)));                               // ID
  params.push_back(se1);                                                       // value1
  params.push_back(se2);                                                       // value2
  params.push_back(Builder.CreateBitCast(g.dest_gep, Builder.getInt8PtrTy())); // destiny GEP
  params.push_back(Builder.CreateBitCast(g.op_gep, Builder.getInt8PtrTy()));   // destiny GEP
  params.push_back(Builder.getInt32(g.operand_pos));                           // Operand pos (1 or 2)
  CallInst *call = Builder.CreateCall(f, params);
}

Optional<StoreInst*> Count::can_reach_store(Instruction *I) {
  assert(is_arith_inst_of_interest(I));

  for (Value *op : I->users()){
    if (StoreInst *si = dyn_cast<StoreInst>(op)){
      return si;
    }
  }

  return None;
}


// Let's just check if the operands of the two instructiosn are the same 
bool Count::check_operands_equals(const Value *vu, const Value *vv){

  if (vu == vv)
    return true;

  const Instruction *u = dyn_cast<Instruction>(vu);
  const Instruction *v = dyn_cast<Instruction>(vv);

  if (u->getNumOperands() != v->getNumOperands())
    return false;

  for (unsigned i = 0; i < u->getNumOperands(); i++)
    if (u->getOperand(i) != v->getOperand(i))
      return false;

  // Insane check
  if (u->getType() != v->getType())
    return false;
  
  return true;
}


// This method checks if op was produced by a LoadInst whose
// the pointer loaded is the same as the dest_gep
// Given that every GetElementPtrInst has a %base pointer as
// as well as an %offset, we just compare them.
Optional<GetElementPtrInst*> Count::check_op(Value *op, GetElementPtrInst *dest_gep){
  if (!isa<LoadInst>(op))
    return None;

  LoadInst *li = dyn_cast<LoadInst>(op);

  // To-Do, check for types other than GetElementPtrInst
  if (!isa<GetElementPtrInst>(li->getPointerOperand()))
    return None;

  GetElementPtrInst *op_gep = dyn_cast<GetElementPtrInst>(li->getPointerOperand());

  // Check 4: Pointers should be from the same basic block
  if (dest_gep->getParent() != op_gep->getParent())
    return None;

  // Check 5: both geps should have the same type
  if (dest_gep->getType() != op_gep->getType())
    return None;

  // errs() << "Check\n";
  // errs() << *dest_gep << " -- " << *dest_gep->getOperand(1) << "\n";
  // errs() << *op_gep << " -- " << *op_gep->getOperand(1) << "\n";

  if (dest_gep->getOperand(0) == op_gep->getOperand(0) && // Checks if the base pointer are the same
      check_operands_equals(dest_gep->getOperand(1), op_gep->getOperand(1))){
    // errs() << "opa\n";
    return op_gep;
  }

  return None;
}

Optional<Geps> Count::good_to_go(Instruction *I){
  // Given I as
  //   I: %dest = `op` %a, %b
  //
  // There are 5 conditions that should be met:
  //  1. `I` should be an arithmetic instruction of interest
  //
  //  2. %dest MUST be used in a store:
  //    store %dest, %ptr
  //
  //  3. either %a or %b must be loaded from the same %ptr
  //    ptr = getElementPtr %base, %offset
  //  Both %base and %offset should be the same
  // 
  //  4. Both instructions must be on the same basic block!
  //      while (x > 0) {
  //        y = gep p, 0, x
  //      }
  //      ...
  //      z = gep p, 0, x
  //    In the case above, geps are the same but the first one will
  //    not have the same value all the time! Therefore, it's important
  //    that we only check for geps that are only on the same basic block!
  //
  //  5. Both geps should be of the same type!
  //       p = global int
  //       y = gep p, 0, x
  //       z = gep cast p to char*, 0, x
  //     In the case above, both geps will hold diferent values since the first
  //     is a gep for an int* and the second for a char*
  // 
  //  Idea: Use RangeAnalysis here to check the offset? Maybe!?
  //  If we use RangeAnalysis, we can drop check 4 when the base pointers are the same

  // Check 1
  if (!is_arith_inst_of_interest(I))
    return None;

  // Check 2
  Optional<StoreInst*> si = can_reach_store(I);
  if (!si)
    return None;

  // To-Do: Check for other types? I know that %ptr can be a global variable
  if(!isa<GetElementPtrInst>((*si)->getPointerOperand()))
    return None;

  GetElementPtrInst *dest_gep =
      dyn_cast<GetElementPtrInst>((*si)->getPointerOperand());

  // Check 3: 
  // Perform a check on both operands
  Value *a = I->getOperand(0);
  Value *b = I->getOperand(1);

  // Checks 4 and 5 are done on `check_op` function

  if (Optional<GetElementPtrInst*> op_gep = check_op(a, dest_gep)){
    return Geps(dest_gep, *op_gep, *si, FIRST);
  }
  else if (Optional<GetElementPtrInst*> op_gep = check_op(b, dest_gep)){
    return Geps(dest_gep, *op_gep, *si, SECOND);
  }

  return None;
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
        if (Optional<Geps> g = good_to_go(&I)) {

          if (I.getOperand(0)->getType()->isVectorTy() ||
              I.getOperand(1)->getType()->isVectorTy())
            errs() << "Vector: " << I << "\n";

          if (I.getType()->isFloatingPointTy()) {
            track_float(M, &I, I.getOperand(0), I.getOperand(1), *g);
          } else
            track_int(M, &I, I.getOperand(0), I.getOperand(1), *g);
        }
      }
    }
  }

  // dump(add_map);
  // dump(fadd_map);
  // dump(fmul_map);
  // dump(fsub_map);

  return false;
}

void Count::getAnalysisUsage(AnalysisUsage &AU) const { AU.setPreservesAll(); }

char Count::ID = 0;
static RegisterPass<Count> X("CountArith", "Count pattern a = a OP b");
