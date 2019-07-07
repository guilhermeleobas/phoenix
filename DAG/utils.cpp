#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"  // To use the iterator instructions(f)
#include "llvm/IR/Instructions.h"  // To have access to the Instructions.
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/IR/DebugInfoMetadata.h"

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
        assert(VMap[incoming] && "VMap does not hold an entry for the incoming block");
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

Function *CloneFunction(Function *F, ValueToValueMapTy &VMap, const Twine &name) {
  std::vector<Type *> ArgTypes;

  // The user might be deleting arguments to the function by specifying them in
  // the VMap.  If so, we need to not add the arguments to the arg ty vector
  //
  for (const Argument &I : F->args())
    if (VMap.count(&I) == 0)  // Haven't mapped the argument to anything yet?
      ArgTypes.push_back(I.getType());

  // Create a new function type...
  auto *I1Ty = Type::getInt1Ty(F->getContext());
  FunctionType *FTy = FunctionType::get(I1Ty, ArgTypes, F->getFunctionType()->isVarArg());

  // Create the new function...
  Function *NewF = Function::Create(FTy, F->getLinkage(), name, F->getParent());

  // Loop over the arguments, copying the names of the mapped arguments over...
  Function::arg_iterator DestI = NewF->arg_begin();
  for (const Argument &I : F->args())
    if (VMap.count(&I) == 0) {      // Is this argument preserved?
      DestI->setName(I.getName());  // Copy the name over...
      VMap[&I] = &*DestI++;         // Add mapping to VMap
    }

  SmallVector<ReturnInst *, 8> Returns;  // Ignore returns cloned.
  CloneFunctionInto(NewF, F, VMap, F->getSubprogram() != nullptr, Returns, ".s", nullptr);

  return NewF;
}

Function* get_abs(Module *mod, Type *FT){
  std::vector<Type*> types;
  types.push_back(FT);
  Function *fabs = Intrinsic::getDeclaration(mod, Intrinsic::fabs, types);
  assert (fabs);
  return fabs;
}

Function* get_rand(Module *mod){
  const StringRef fname = "rand";
  Function *func_rand = mod->getFunction(fname);
  if (!func_rand) {
    FunctionType *FuncTy9 = FunctionType::get(IntegerType::get(mod->getContext(), 32), true);

    func_rand = Function::Create(FuncTy9, GlobalValue::ExternalLinkage, fname, mod);
    func_rand->setCallingConv(CallingConv::C);
  }
  return func_rand;
}

Function* get_printf(Module *mod){
  const StringRef fname = "printf";
  Function *func = mod->getFunction(fname);
  if (!func) {
    PointerType *Pty = PointerType::get(IntegerType::get(mod->getContext(), 8), 0);
    FunctionType *FuncTy9 = FunctionType::get(IntegerType::get(mod->getContext(), 32), true);
    func = Function::Create(FuncTy9, GlobalValue::ExternalLinkage, fname, mod);
    func->setCallingConv(CallingConv::C);
  }
  return func;
}

void add_dump_msg(Instruction *I, const StringRef &msg){
  BasicBlock *BB = I->getParent();
  Module *mod = BB->getModule();
  Function *func_printf = get_printf(mod);
  IRBuilder<> Builder(mod->getContext());
  Builder.SetInsertPoint(BB->getTerminator());

  Value *str = Builder.CreateGlobalStringPtr(msg);
  std::vector<Value *> int32_call_params;
  int32_call_params.push_back(str);

  Builder.CreateCall(func_printf, int32_call_params, "call_printf_dump");

}

void add_dump_msg(Instruction *I, const StringRef &msg, Value *V) {
  BasicBlock *BB = I->getParent();
  Module *mod = BB->getModule();
  Function *func_printf = get_printf(mod);
  IRBuilder<> Builder(mod->getContext());
  Builder.SetInsertPoint(BB->getTerminator());

  Value *str = Builder.CreateGlobalStringPtr(msg);
  std::vector<Value *> int32_call_params;
  int32_call_params.push_back(str);

  if (V->getType()->isFloatingPointTy()){
    V = Builder.CreateFPExt(V, Type::getDoubleTy(I->getContext()));
  }

  int32_call_params.push_back(V);

  Builder.CreateCall(func_printf, int32_call_params, "call_printf_dump");
}

void add_dump_msg(BasicBlock *BB, const StringRef &msg){
  add_dump_msg(BB->getTerminator(), msg);
}

void add_dump_msg(BasicBlock *BB, const StringRef &msg, Value *V){
  add_dump_msg(BB->getTerminator(), msg, V);
}

void print_instruction(Instruction *I) {
  const DebugLoc &loc = I->getDebugLoc();
  if (loc) {
    auto *Scope = cast<DIScope>(loc.getScope());
    errs() << "I: " << *I << " [" << Scope->getFilename() << ":" << loc.getLine() << "]"
           << "\n";
  }
  else {
    errs() << *I << "\n";
  }
}


}  // namespace phoenix
