#pragma once

#include "Position.h"

// This instruction encapsulates the necessary things to keep track of
// instructions that follows the patterns that we are looking for
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

  // The insertion point
  StoreInst *store;

  Geps(GetElementPtrInst *dest, GetElementPtrInst *op, StoreInst *si,
       Instruction *i, unsigned pos)
      : dest_gep(dest), op_gep(op), store(si), I(i), operand_pos(pos) {
    assert(operand_pos == FIRST || operand_pos == SECOND);
  }
};
