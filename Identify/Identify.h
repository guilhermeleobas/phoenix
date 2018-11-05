#pragma once

using namespace llvm;

#include "Position.h"

class Identify : public FunctionPass {
private:

  vector<Geps> instructions_of_interest;

  // Check if the instruction I is an arithmetic instruction
  // of interest. We don't instrument Floating-Point instructions
  // because they don't have an identity.
  bool is_arith_inst_of_interest(Instruction *I);

  // Check if the current instruction I can reach a store following the control
  // flow graph. Note that I must pass the `is_arith_inst_of_interest` test
  Optional<StoreInst *> can_reach_store(Instruction *I);

  //
  bool check_operands_equals(const Value *vu, const Value *vv);
  Optional<GetElementPtrInst*> check_op(Value *op, GetElementPtrInst *dest_gep);
  Optional<Geps> good_to_go(Instruction *I);

public:
  // Pass identifier, for LLVM's RTTI support:
  static char ID;

  bool runOnFunction(Function &);


  void getAnalysisUsage(AnalysisUsage &AU) const;

  Identify() : FunctionPass(ID) {}
  ~Identify() {}
};
