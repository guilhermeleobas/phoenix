#pragma once

#include "llvm/ADT/Statistic.h" // For the STATISTIC macro.
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/IR/Constants.h"         // For ConstantData, for instance.
#include "llvm/IR/DebugInfoMetadata.h" // For DILocation
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h" // To use the iterator instructions(f)
#include "llvm/IR/Instructions.h" // To have access to the Instructions.
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h" // To print error messages.
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h" // For dbgs()
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"

using namespace llvm;

void remap_nodes(BasicBlock *BB, ValueToValueMapTy &VMap){
  for (Instruction &I : *BB) {
    for (unsigned i = 0; i < I.getNumOperands(); i++) {
      Value *op = I.getOperand(i);
      if (VMap.find(op) != VMap.end()) {
        I.setOperand(i, VMap[op]);
      }

      // also update incoming blocks
      if (PHINode *phi = dyn_cast<PHINode>(&I)){
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

