#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"  // To use the iterator instructions(f)
#include "llvm/IR/Instructions.h"  // To have access to the Instructions.
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
        BasicBlock *incoming = phi->getIncomingBlock(i);
        phi->setIncomingBlock(i, cast<BasicBlock>(VMap[incoming]));
      }
    }
  }
}

BasicBlock *deep_clone(const BasicBlock *BB,
                       ValueToValueMapTy &VMap,
                       const Twine &suffix,
                       Function *F) {
  BasicBlock *clone = llvm::CloneBasicBlock(BB, VMap, suffix, F);

  remap_nodes(clone, VMap);

  return clone;
}

Function *CloneFunction(Function *F, ValueToValueMapTy &VMap, ClonedCodeInfo *CodeInfo) {
  std::vector<Type *> ArgTypes;

  // The user might be deleting arguments to the function by specifying them in
  // the VMap.  If so, we need to not add the arguments to the arg ty vector
  //
  for (const Argument &I : F->args())
    if (VMap.count(&I) == 0)  // Haven't mapped the argument to anything yet?
      ArgTypes.push_back(I.getType());

  // Create a new function type...
  auto *I32Ty = Type::getInt32Ty(F->getContext());
  FunctionType *FTy = FunctionType::get(I32Ty, ArgTypes, F->getFunctionType()->isVarArg());

  // Create the new function...
  Function *NewF = Function::Create(FTy, F->getLinkage(), F->getName(), F->getParent());

  // Loop over the arguments, copying the names of the mapped arguments over...
  Function::arg_iterator DestI = NewF->arg_begin();
  for (const Argument &I : F->args())
    if (VMap.count(&I) == 0) {      // Is this argument preserved?
      DestI->setName(I.getName());  // Copy the name over...
      VMap[&I] = &*DestI++;         // Add mapping to VMap
    }

  SmallVector<ReturnInst *, 8> Returns;  // Ignore returns cloned.
  CloneFunctionInto(NewF, F, VMap, F->getSubprogram() != nullptr, Returns, "", CodeInfo);

  return NewF;
}



}  // namespace phoenix