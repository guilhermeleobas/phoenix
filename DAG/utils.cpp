#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h" // To use the iterator instructions(f)
#include "llvm/IR/Instructions.h" // To have access to the Instructions.
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"

using namespace llvm;

namespace phoenix {
void remap_nodes(BasicBlock *BB, ValueToValueMapTy &VMap) {
  for (Instruction &I : *BB) {
    for (unsigned i = 0; i < I.getNumOperands(); i++) {
      Value *op = I.getOperand(i);
      if (VMap.find(op) != VMap.end()) {
        I.setOperand(i, VMap[op]);
      }

      // also update incoming blocks
      if (PHINode *phi = dyn_cast<PHINode>(&I)) {
        auto *incoming = phi->getIncomingBlock(i);
        phi->setIncomingBlock(i, cast<BasicBlock>(VMap[incoming]));
      }
    }
  }
}

BasicBlock *deep_clone(const BasicBlock *BB, ValueToValueMapTy &VMap,
                       const Twine &suffix, Function *F) {
  BasicBlock *clone = llvm::CloneBasicBlock(BB, VMap, suffix, F);

  remap_nodes(clone, VMap);

  return clone;
}

} // namespace phoenix