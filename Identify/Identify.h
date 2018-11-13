#pragma once

using namespace llvm;

#include <vector>

#include "Position.h"
#include "Geps.h"

class Identify : public FunctionPass {
private:

  llvm::SmallVector<Geps, 10> instructions_of_interest;

  // Check if the instruction I is an arithmetic instruction
  // of interest. We don't instrument Floating-Point instructions
  // because they don't have an identity.
  bool is_arith_inst_of_interest(Instruction *I);

  // Find a LoadInst that can reach the value *v
  std::vector<LoadInst*> find_load_inst(Instruction *I, Value *v);

  // Check if the current instruction I can reach a store following the control
  // flow graph. Note that I must pass the `is_arith_inst_of_interest` test
  Optional<StoreInst *> can_reach_store(Instruction *I);

  //
  bool check_operands_equals(const Value *vu, const Value *vv);
  Optional<GetElementPtrInst*> check_op(LoadInst *li, GetElementPtrInst *dest_gep);
  Optional<Geps> good_to_go(Instruction *I);

  // gather info about I
  void set_loop_depth(LoopInfo *LI, Geps &g);

public:
  // Pass identifier, for LLVM's RTTI support:
  static char ID;

  bool runOnFunction(Function &);
  void getAnalysisUsage(AnalysisUsage &AU) const;

  llvm::SmallVector<Geps, 10> get_instructions_of_interest();

  Identify() : FunctionPass(ID) {}
  ~Identify() {}
};
