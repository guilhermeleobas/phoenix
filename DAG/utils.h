#pragma once

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h" // To use the iterator instructions(f)
#include "llvm/IR/Instructions.h" // To have access to the Instructions.
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"

using namespace llvm;

namespace phoenix {
void remap_nodes(BasicBlock *BB, ValueToValueMapTy &VMap);

BasicBlock *deep_clone(const BasicBlock *BB, ValueToValueMapTy &VMap,
                       const Twine &suffix, Function *F);

Function *CloneFunction(Function *F, ValueToValueMapTy &VMap, ClonedCodeInfo *CodeInfo = nullptr);
}; // end namespace phoenix