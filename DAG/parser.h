#pragma once

#include "node.h"

using namespace llvm;

phoenix::Node* myParser(BasicBlock *BB, Value *V);
phoenix::Node* myParser(Instruction *I);
void dumpExpression(phoenix::Node *node);