#pragma once

#include "Position.h"

// This instruction encapsulates the necessary things to keep track of
// instructions that follows the patterns that we are looking for
// The instructions of interest are of the form:
//   I: *p = *p `op` v, 
// where `op` can be +, -, *, /, >>, <<, ...
struct Geps {
public:
  // The GetElementPtrInst instructions below are used by the profiler
  // to check if the addresses are the same at runtime. Just a sanity check
  // to see how accurate our static analysis is!
  GetElementPtrInst *dest_gep;
  GetElementPtrInst *op_gep;

  // The instruction where all this mess came from
  Instruction *I;

  // Given that I is of the form:
  // I: %res = op %a, %b
  // This integer value tell us wether %a or %b is the register that refers to
  // *p This
  unsigned operand_pos;

  // A pointer to the instruction that stores *p (LHS)
  StoreInst *store;

  // A pointer to the instruction that loads *p (RHS)
  LoadInst *load;

  Geps(GetElementPtrInst *dest, GetElementPtrInst *op, StoreInst *si, LoadInst *l,
       Instruction *i, unsigned pos)
      : dest_gep(dest), op_gep(op), store(si), load(l), I(i), operand_pos(pos) {
    assert(operand_pos == FIRST || operand_pos == SECOND);
  }

};







