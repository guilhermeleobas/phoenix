#pragma once

#include "node.h"

using namespace llvm;

// @pos indicates which operand is the LoadInst that loads
// from the same memory position that @store is storing! 
// geps.get_operand_pos() returns this number!
phoenix::Node* myParser(BasicBlock *BB, Value *V, unsigned pos);
phoenix::Node* myParser(StoreInst *store, unsigned pos);
void dumpExpression(phoenix::Node *node);
void dumpDot(phoenix::Node *node);