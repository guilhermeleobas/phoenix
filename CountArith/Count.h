#pragma once

using namespace llvm;

#include <map>
#include "Position.h"

using std::map;

struct Geps {
  GetElementPtrInst *dest_gep;
  GetElementPtrInst *op_gep;

  StoreInst *store;

  unsigned operand_pos;

  Geps(GetElementPtrInst *dest, GetElementPtrInst *op, StoreInst *si, unsigned pos)
      : dest_gep(dest), op_gep(op), store(si), operand_pos(pos) {}
};

class Count : public ModulePass {
private:
  map<Instruction *, unsigned> add_map, sub_map, mul_map, xor_map, fadd_map,
      fsub_map, fmul_map;

public:
  // Pass identifier, for LLVM's RTTI support:
  static char ID;

  bool runOnModule(Module &);

  // Debugging method
  void print_instructions(Module &M);
  void dump(const map<Instruction *, unsigned> &mapa);

  void insert_dump_call(Module &M, Instruction *I);
  void insert_dump_call(Module &M);

  // Assign a unique ID to each arithmetic instruction of the type:
  //  - Add, Sub, Mul, Xor
  unsigned get_id(map<Instruction *, unsigned> &mapa, Instruction *I);
  unsigned get_id(Instruction *I);

  // Adds a function call to a function defined externally which checks
  // if op1 or op2 are the identity value of the instruction I
  // Instruction I is **always** an arithmetic instruction
  void track_int(Module &M, Instruction *I, Value *op1, Value *op2, Geps &g);
  void track_float(Module &M, Instruction *I, Value *op1, Value *op2, Geps &g);

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

  void getAnalysisUsage(AnalysisUsage &AU) const;

  Count() : ModulePass(ID) {}
  ~Count() {}
};
