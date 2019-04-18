#include "llvm/ADT/Statistic.h"  // For the STATISTIC macro.
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/IR/Constants.h"          // For ConstantData, for instance.
#include "llvm/IR/DebugInfoMetadata.h"  // For DILocation
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
#include "llvm/Transforms/Utils/Cloning.h"

#include <queue>
#include <tuple>

#include "DAG.h"
#include "constraintVisitor.h"
#include "depthVisitor.h"
#include "dotVisitor.h"
#include "insertIf.h"

#define DEBUG_TYPE "DAG"
#define PROFILE_FIRST false


Instruction *DAG::create_switch(Function *F, BasicBlock *BB,
                                BasicBlock *BBProfile, BasicBlock *BBOpt) {
#define DEFAULT_SWITCH_VALUE 0

  // 1. Create a variable to control the switch
  IRBuilder<> Builder(F->getEntryBlock().getFirstNonPHI());
  Instruction *switch_control = Builder.CreateAlloca(Type::getInt8Ty(F->getContext()),
                                             nullptr, "switch.control");
  Builder.CreateStore(
      ConstantInt::get(Type::getInt8Ty(F->getContext()), DEFAULT_SWITCH_VALUE),
      switch_control);

  // 2. Create the switch with a default jump to BBProfile
  BasicBlock *BBSwitch =
      BasicBlock::Create(F->getContext(), "Switch", F, BBProfile);
  Builder.SetInsertPoint(BBSwitch);
  Instruction *load = Builder.CreateLoad(switch_control);
  SwitchInst *si = Builder.CreateSwitch(load, BBProfile, 3);

  // Case with 1 with a jump to BB
  si->addCase(ConstantInt::get(Type::getInt8Ty(F->getContext()), 1), BB);

  // Case 2 with a jump to the optimized BB
  si->addCase(ConstantInt::get(Type::getInt8Ty(F->getContext()), 2), BBOpt);

  // Iterate over every predecessor of BB and changes it's jump to switchBB
  for (BasicBlock *pred : predecessors(BB)) {
    if (pred == BBSwitch) continue;
    TerminatorInst *TI = pred->getTerminator();

    switch (TI->getOpcode()) {
      case Instruction::Br:
        if (TI->getSuccessor(0) == BB)
          TI->setSuccessor(0, BBSwitch);
        else
          TI->setSuccessor(1, BBSwitch);
        break;
      case Instruction::IndirectBr:
      default:
        std::string str = "Predecessor with terminator inst: ";
        llvm::raw_string_ostream rso(str);
        TI->print(rso);
        llvm_unreachable(str.c_str());
    }
  }

  return switch_control;
}

// Since cloneBasicBlock performs a shallow copy of BB.
// This method clones BB and replaces each ocurrences
// of values with the ones in the ValueToValueMapTy
// Basically, a deep clone!
BasicBlock *DAG::deep_clone(BasicBlock *BB, ValueToValueMapTy &VMap,
                            const Twine &suffix, Function *F) {
  BasicBlock *clone = llvm::CloneBasicBlock(BB, VMap, suffix, F);

  for (Instruction &I : *clone) {
    for (unsigned i = 0; i < I.getNumOperands(); i++) {
      Value *op = I.getOperand(i);
      if (VMap.find(op) != VMap.end()) {
        I.setOperand(i, VMap[op]);
      }
    }
  }

  return clone;
}

// create and increment C1 whenever the control flow reaches BBProfile 
AllocaInst* DAG::create_c1(Function *F, BasicBlock *BBProfile){
  IRBuilder<> Builder(F->getEntryBlock().getFirstNonPHI());

  auto *I8Ty = Type::getInt8Ty(F->getContext());
  auto *zero = ConstantInt::get(I8Ty, 0);
  auto *one = ConstantInt::get(I8Ty, 1);

  AllocaInst *c1_ptr = Builder.CreateAlloca(I8Ty, nullptr, "c1_ptr." + BBProfile->getName());
  Builder.CreateStore(zero, c1_ptr);

  // c1 inc
  Builder.SetInsertPoint(BBProfile->getFirstNonPHI());

  LoadInst *load_c1 = Builder.CreateLoad(c1_ptr, "c1.load");
  Value *c1_inc = Builder.CreateAdd(load_c1, one, "c1.inc");
  Builder.CreateStore(c1_inc, c1_ptr);

  return c1_ptr;
}

// Create and increment c2 when V == constraint
AllocaInst* DAG::create_c2(Function *F, BasicBlock *BBProfile, Value *V, Value *constraint){
  IRBuilder<> Builder(F->getEntryBlock().getFirstNonPHI());

  auto *I8Ty = Type::getInt8Ty(F->getContext());
  auto *zero = ConstantInt::get(I8Ty, 0);
  auto *one = ConstantInt::get(I8Ty, 1);

  AllocaInst *c2_ptr = Builder.CreateAlloca(I8Ty, nullptr, "c2_ptr." + BBProfile->getName());
  Builder.CreateStore(zero, c2_ptr);

  // c2 inc
  Builder.SetInsertPoint(cast<Instruction>(V)->getNextNode());

  Value *cmp;
  if (V->getType()->isFloatingPointTy())
    cmp = Builder.CreateFCmpONE(V, constraint, "cmp.profile");
  else
    cmp = Builder.CreateICmpNE(V, constraint, "cmp.profile");

  Value *select = Builder.CreateSelect(cmp, one, zero);

  LoadInst *load_c2 = Builder.CreateLoad(c2_ptr, "c2.load");
  Value *c2_inc = Builder.CreateAdd(load_c2, select, "c2.inc");
  Builder.CreateStore(c2_inc, c2_ptr);

  return c2_ptr;
}

// @BB is the Basic Block that one will clone
// @V is the value that one will compare against cnt
// @constraint is obviously the constraint
//
// This method also remaps V to the Value* on BBProfile
//
std::tuple<BasicBlock*, Value*, Value*> DAG::create_BBProfile(Function *F, BasicBlock *BB,
                                            Value *V, Value *constraint) {
  ValueToValueMapTy VMap;
  BasicBlock *BBProfile = deep_clone(BB, VMap, ".profile", F);

  Value *c1 = create_c1(F, BBProfile);
  Value *c2 = create_c2(F, BBProfile, cast<Value>(VMap[V]), constraint);

  return std::make_tuple(BBProfile, c1, c2);
}

//
BasicBlock *DAG::create_BBOpt(Function *F, BasicBlock *BB,
                                        StoreInst *store, Value *V,
                                        Value *constraint) {
  ValueToValueMapTy VMap;
  BasicBlock *BBOpt = deep_clone(BB, VMap, ".opt", F);

  insert_if(cast<StoreInst>(VMap[store]), VMap[V], constraint);

  return BBOpt;
}

// Creates a BasicBlock right after BBProfile that controls the control flow
// from BBProfile to BB or BBOpt
void DAG::create_BBControl(Function *F, BasicBlock *BBProfile,
                      Value *switch_control, Value *c1, Value *c2,
                      ConstantInt *n_iter, ConstantInt *gap){

  

}

void DAG::create_BBControl(Function *F, BasicBlock *BBProfile, Value *switch_control, Value *c1, Value *c2){
  #define N_ITER 1000
  #define GAP 501

  auto *I32Ty = Type::getInt8Ty(F->getContext());
  auto *n_iter = ConstantInt::get(I32Ty, N_ITER);
  auto *gap = ConstantInt::get(I32Ty, GAP);

  create_BBControl(F, BBProfile, switch_control, c1, c2, n_iter, gap);
}

// 
void DAG::profile_and_optimize(Function *F, const Geps &g,
                               const phoenix::Node *node, bool justOptimize) {
  BasicBlock *BB = node->getInst()->getParent();
  BasicBlock *BBOpt = create_BBOpt(F, BB, g.get_store_inst(), node->getValue(), node->getConstraint());

  if (!justOptimize) {
    assert(node->hasConstraint() && "Node do not have a constraint");

    Value *V = node->getValue();
    Value *constraint = node->getConstraint();

    auto tupla = create_BBProfile(F, BB, V, constraint);
    BasicBlock *BBProfile = std::get<0>(tupla);
    Value *c1 = std::get<1>(tupla);
    Value *c2 = std::get<2>(tupla);

    Value *switch_control = create_switch(F, BB, BBProfile, BBOpt);

    create_BBControl(F, BBProfile, switch_control, c1, c2);
  }
}

//
void DAG::runDAGOptimization(Function &F, llvm::SmallVector<Geps, 10> &gs) {
  for (auto &g : gs) {
    Instruction *I = g.get_instruction();

    // sanity check for vector instructions
    if (I->getOperand(0)->getType()->isVectorTy() ||
        I->getOperand(1)->getType()->isVectorTy()) {
      continue;
    }

    if (!worth_insert_if(g)) continue;

    phoenix::StoreNode *store =
        cast<phoenix::StoreNode>(myParser(g.get_store_inst()));

    ConstraintVisitor cv(store, &g);
    DepthVisitor dv(store);

    std::set<phoenix::Node *, NodeCompare> *s = dv.getSet();

    for (auto node : reverse(*s)) {
      DotVisitor dv(store);
      dv.print();

      profile_and_optimize(&F, g, node, false);

      break;  // Let's just insert on the first element;
    }
  }
}

//
bool DAG::runOnFunction(Function &F) {
  if (F.isDeclaration() || F.isIntrinsic() || F.hasAvailableExternallyLinkage())
    return true;

  Identify *Idn = &getAnalysis<Identify>();

  llvm::SmallVector<Geps, 10> g = Idn->get_instructions_of_interest();
  runDAGOptimization(F, g);

  return false;
}

//
void DAG::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<Identify>();
  AU.addRequired<LoopInfoWrapperPass>();
  AU.setPreservesAll();
}

char DAG::ID = 0;
static RegisterPass<DAG> X("DAG", "DAG pattern a = a OP b", false, false);
