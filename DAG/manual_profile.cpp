#include "llvm/ADT/Statistic.h"  // For the STATISTIC macro.
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/IR/Constants.h"          // For ConstantData, for instance.
#include "llvm/IR/DebugInfoMetadata.h"  // For DILocation
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"  // To use the iterator instructions(f)
#include "llvm/IR/Instructions.h"  // To have access to the Instructions.
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"  // To print error messages.
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"  // For dbgs()
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include <queue>

#include "../ProgramSlicing/ProgramSlicing.h"
#include "manual_profile.h"
#include "utils.h"

using namespace llvm;

namespace phoenix {

Function *PerformClone(Function *F,
                              ValueToValueMapTy &VMap,
                              ClonedCodeInfo *CodeInfo = nullptr) {
  std::vector<Type *> ArgTypes;

  // The user might be deleting arguments to the function by specifying them in
  // the VMap.  If so, we need to not add the arguments to the arg ty vector
  //
  for (const Argument &I : F->args())
    if (VMap.count(&I) == 0)  // Haven't mapped the argument to anything yet?
      ArgTypes.push_back(I.getType());

  // Create a new function type...
  auto *I32Ty = Type::getInt32Ty(F->getContext());
  FunctionType *FTy = FunctionType::get(I32Ty, ArgTypes,
                                        F->getFunctionType()->isVarArg());

  // Create the new function...
  Function *NewF =
      Function::Create(FTy, F->getLinkage(), F->getName(), F->getParent());

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

// Clone the function
static Function *clone_function(Function *F, ValueToValueMapTy &VMap) {
  Function *clone = PerformClone(F, VMap);
  clone->setLinkage(Function::AvailableExternallyLinkage);

  return clone;
}

static void slice_function(ProgramSlicing *PS, Function *clone, Instruction *target) {
  PS->slice(clone, target);
}

// slice, add counter, return value
static Instruction *create_counter(Function *C) {
  auto *I32Ty = Type::getInt32Ty(C->getContext());

  IRBuilder<> Builder(C->getEntryBlock().getFirstNonPHI());
  Instruction *ptr = Builder.CreateAlloca(I32Ty, nullptr, "ptr");
  Builder.CreateStore(ConstantInt::get(I32Ty, 0), ptr);

  return ptr;
}

static void increment_counter(Function *C, Instruction *target, Instruction *ptr, Value *constant) {
  IRBuilder<> Builder(target->getNextNode());

  auto *I32Ty = Type::getInt32Ty(C->getContext());
  auto *zero = ConstantInt::get(I32Ty, 0);
  auto *one = ConstantInt::get(I32Ty, 1);

  LoadInst *counter = Builder.CreateLoad(ptr, "counter");

  Value *cmp;
  if (target->getType()->isFloatingPointTy())
    cmp = Builder.CreateFCmpOEQ(target, constant, "cmp");
  else
    cmp = Builder.CreateICmpEQ(target, constant, "cmp");

  Value *select = Builder.CreateSelect(cmp, zero, one);
  Value *inc = Builder.CreateAdd(counter, select, "inc");
  Builder.CreateStore(inc, ptr);
}

static void change_return(Function *C, Instruction *ptr) {
  for (auto &BB : *C) {
    if (ReturnInst *ri = dyn_cast<ReturnInst>(BB.getTerminator())) {
      IRBuilder<> Builder(ri);
      Instruction *counter = Builder.CreateLoad(ptr, "counter");
      Builder.CreateRet(counter);

      ri->dropAllReferences();
      ri->eraseFromParent();
    }
  }
}

static void add_counter(Function *C, Instruction *target, Value *constant) {
  Instruction *ptr = create_counter(C);
  increment_counter(C, target, ptr, constant);
  change_return(C, ptr);
}

void manual_profile(Function *F,
                    LoopInfo *LI,
                    DominatorTree *DT,
                    PostDominatorTree *PDT,
                    ProgramSlicing *PS,
                    const Geps &g,
                    NodeSet &s) {
  for (auto &target_node : s) {
    ValueToValueMapTy VMap;

    Function *clone = clone_function(F, VMap);
    Instruction *target = cast<Instruction>(VMap[target_node->getInst()]);
    slice_function(PS, clone, target);
    add_counter(clone, target, target_node->getConstant());
    clone->viewCFG();
  }

  // StoreInst *store = cast<StoreInst>(st->VMap[g.get_store_inst()]);
  // for (auto &node : s){

  // auto x = get_function_types(node);

  // Value *V = node->getValue();
  // Value *cnt = node->getConstant();
  // insert_if(store, st->VMap[V], cnt);
  // }
}

}  // namespace phoenix