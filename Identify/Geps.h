#pragma once

#include "Position.h"

// This instruction encapsulates the necessary things to keep track of
// instructions that follows the patterns that we are looking for
// The instructions of interest are of the form:
//   I: *p = *p `op` v,
// where `op` can be +, -, *, /, >>, <<, ...
struct Geps {
private:
  // The GetElementPtrInst instructions below are used by the profiler
  // to check if the addresses are the same at runtime. Just a sanity check
  // to see how accurate our static analysis is!
  GetElementPtrInst *dest_gep;
  GetElementPtrInst *op_gep;

  // The instruction where all this mess came from
  //   I: *p_after = *p_before `op` v
  // *p_before and *p_after refers to the same memory address
  Instruction *I;
  Value *p_before;
  Value *p_after;
  Value *v;

  // Is *p_before the first or the second operand?
  unsigned operand_pos;

  // A pointer to the instruction that stores *p (LHS)
  StoreInst *store;

  unsigned loop_depth;

public:
  Geps(GetElementPtrInst *dest, GetElementPtrInst *op, StoreInst *si,
   Instruction *I, unsigned pos)
  : dest_gep(dest), op_gep(op), store(si), I(I), operand_pos(pos), loop_depth(0) {
    assert(operand_pos == FIRST || operand_pos == SECOND);

    p_after = I;
    p_before = I->getOperand(operand_pos - 1);
    v = I->getOperand(operand_pos == FIRST ? 1 : 0);
  }

  GetElementPtrInst *get_dest_gep() const { return dest_gep; }
  GetElementPtrInst *get_op_gep() const { return op_gep; }
  StoreInst *get_store_inst() const { return store; }
  unsigned get_operand_pos() const { return operand_pos; }
  unsigned get_v_pos() const { return operand_pos == FIRST ? SECOND : FIRST; }
  Value *get_v() const { return v; }
  Instruction *get_v_as_inst() const { return dyn_cast<Instruction>(v); }
  Value *get_p_before() const { return p_before; }
  Value *get_p_after() const { return p_after; }
  Instruction *get_instruction() const { return I; }

  unsigned get_loop_depth() const { return loop_depth; }
  void set_loop_depth(unsigned depth) { loop_depth = depth; }

  void print_instruction() const {
    const DebugLoc &loc = I->getDebugLoc();
    if (loc) {
      auto *Scope = cast<DIScope>(loc.getScope());
      dbgs() << "I: " << *I << " [" << Scope->getFilename()
             << ":" << loc.getLine() << "]"
             << "\n";
    }
  }



};
