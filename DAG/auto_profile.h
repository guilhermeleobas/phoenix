#pragma once

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

#include "insertIf.h"
#include "utils.h"

namespace phoenix {

#define SWITCH_NUM_CASES 3
#define SWITCH_BB_PROFILE_INDEX 0
#define SWITCH_BB_INDEX 1
#define SWITCH_BB_OPT_INDEX 2

// @F : A pointer to the function @BB lives in
// @BB : The original basic block
// @BBProfile : Basic block with counters
// @BBOpt : @BB with conditionals
//
// Creates a new basic block with a switch conditional managed by the Value
// @switch_control : controls to which target the switch will jump
//   0 => jumps to BBProfile (default)
//   1 => jumps to BB
//   2 => jumps to BBOpt
//
// returns the instruction that controls the switch jump target
Instruction *create_switch(Function *F, BasicBlock *BB, BasicBlock *BBProfile, BasicBlock *BBOpt) {
  // 1. Create a variable to control the switch
  IRBuilder<> Builder(F->getEntryBlock().getFirstNonPHI());
  Instruction *switch_control_ptr =
      Builder.CreateAlloca(Type::getInt32Ty(F->getContext()), nullptr, "switch.control_ptr");
  Builder.CreateStore(ConstantInt::get(Type::getInt32Ty(F->getContext()), SWITCH_BB_PROFILE_INDEX),
                      switch_control_ptr);

  // 2. Create the switch with a default jump to BBProfile
  BasicBlock *BBSwitch = BasicBlock::Create(F->getContext(), "Switch", F, BB);
  Builder.SetInsertPoint(BBSwitch);
  Instruction *load = Builder.CreateLoad(switch_control_ptr, "switch_control");
  SwitchInst *si = Builder.CreateSwitch(load, BBProfile, SWITCH_NUM_CASES);

  // Case with 1 with a jump to BB
  si->addCase(ConstantInt::get(Type::getInt32Ty(F->getContext()), SWITCH_BB_INDEX), BB);

  // Case 2 with a jump to the optimized BB
  si->addCase(ConstantInt::get(Type::getInt32Ty(F->getContext()), SWITCH_BB_OPT_INDEX), BBOpt);

  // Iterate over every predecessor of BB and changes it's jump to switchBB
  for (BasicBlock *pred : predecessors(BB)) {
    if (pred == BBSwitch)
      continue;
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

  return switch_control_ptr;
}

// create and increment C1 whenever the control flow reaches BBProfile
AllocaInst *create_c1(Function *F, BasicBlock *BBProfile) {
  IRBuilder<> Builder(F->getEntryBlock().getFirstNonPHI());

  auto *I32Ty = Type::getInt32Ty(F->getContext());
  auto *zero = ConstantInt::get(I32Ty, 0);
  auto *one = ConstantInt::get(I32Ty, 1);

  AllocaInst *c1_ptr = Builder.CreateAlloca(I32Ty, nullptr, "c1_ptr." + BBProfile->getName());
  Builder.CreateStore(zero, c1_ptr);

  // c1 inc
  Builder.SetInsertPoint(BBProfile->getFirstNonPHI());

  LoadInst *load_c1 = Builder.CreateLoad(c1_ptr, "c1.load");
  Value *c1_inc = Builder.CreateAdd(load_c1, one, "c1.inc");
  Builder.CreateStore(c1_inc, c1_ptr);

  return c1_ptr;
}

// Create and increment c2 when V == constant
AllocaInst *create_c2(Function *F, BasicBlock *BBProfile, Value *before, Value *after) {
  IRBuilder<> Builder(F->getEntryBlock().getFirstNonPHI());

  auto *I32Ty = Type::getInt32Ty(F->getContext());
  auto *zero = ConstantInt::get(I32Ty, 0);
  auto *one = ConstantInt::get(I32Ty, 1);

  AllocaInst *c2_ptr = Builder.CreateAlloca(I32Ty, nullptr, "c2_ptr." + BBProfile->getName());
  Builder.CreateStore(zero, c2_ptr);

  // c2 inc
  Builder.SetInsertPoint(cast<Instruction>(after)->getNextNode());

  Value *cmp;
  if (before->getType()->isFloatingPointTy())
    cmp = Builder.CreateFCmpOEQ(before, after, "cmp.profile");
  else
    cmp = Builder.CreateICmpEQ(before, after, "cmp.profile");

  Value *select = Builder.CreateSelect(cmp, zero, one);

  LoadInst *load_c2 = Builder.CreateLoad(c2_ptr, "c2.load");
  Value *c2_inc = Builder.CreateAdd(load_c2, select, "c2.inc");
  Builder.CreateStore(c2_inc, c2_ptr);

  return c2_ptr;
}

// @F : A pointer to the function @BB lives in
// @BB : The original basic block
// @V : The llvm value that "kills" the expression
// @constant: the value that @V must hold to kill the expression
//
// This method creates a copy of @BB, which we called it @BBProfile
// and insert counters c1 and c2 to:
//   - @c1: Count the number of times @BBProfile executes.
//   - @c2: Count the number of times @V == @onstraint
std::pair<Value *, Value *> create_BBProfile(Function *F,
                                             BasicBlock *BBProfile,
                                             Instruction *before,
                                             Instruction *after) {
  Value *c1 = create_c1(F, BBProfile);
  Value *c2 = create_c2(F, BBProfile, before, after);

  return std::make_pair(c1, c2);
}

// @F : A pointer to the function @BB lives in
// @BB : The original basic block
// @store : A pointer to the store that leads to @V
// @V : The llvm value that "kills" the expression
// @constant: the value that @V must hold to kill the expression
//
// Returns a clone of @BB with the conditional. See `insert_if` for more info.
void create_BBOpt(ValueToValueMapTy &VMap, StoreInst *store, Value *V, Value *constant) {
  insert_if(cast<StoreInst>(VMap[store]), VMap[V], constant);
}

// @F : A pointer to the function @BB lives in
// @BBProfile : Basic block with counters
// @switch_control : controls to which target the switch will jump
//   0 => jumps to BBProfile
//   1 => jumps to BB
//   2 => jumps to BBOpt
// @c1 : # of times @BBProfile were executed
// @c2 : # of times @V == @constant on @BBProfile
// @n_iter : max. number of iterations of @BBProfile
// @gap : The minimum difference between @c1 and @c2 to use BBOpt instead of
// BB.
//   - The reasoning behind is: If @c1 ~ @c2, then we use @BB. Otherwise we
//   use @BBProfile.
//   - @gap controls how close @c1 must be from @c2 for us to choose @BB:
//       if (@c1 - @c2) >= @gap then use @BBOpt
//       else use @BB
//
// Creates a basic block that changes the value of @switch_control based
// on the counters.
//
void create_BBControl(Function *F,
                      BasicBlock *BBProfile,
                      Value *switch_control_ptr,
                      Value *c1_ptr,
                      Value *c2_ptr,
                      ConstantInt *n_iter,
                      ConstantInt *gap) {
  auto *I32Ty = Type::getInt32Ty(F->getContext());
  auto *BBOpt_target_value = ConstantInt::get(I32Ty, SWITCH_BB_OPT_INDEX);
  auto *BB_target_value = ConstantInt::get(I32Ty, SWITCH_BB_INDEX);

  auto *one = ConstantInt::get(I32Ty, 1);
  auto *zero = ConstantInt::get(I32Ty, 0);

  // Create BBControl and insert it right after BBProfile
  BasicBlock *BBControl = BBProfile->splitBasicBlock(BBProfile->getTerminator(), "BBControl");
  IRBuilder<> Builder(BBControl->getFirstNonPHI());

  Value *c1 = Builder.CreateLoad(c1_ptr, "c1");
  Value *c2 = Builder.CreateLoad(c2_ptr, "c2");
  Value *sub = Builder.CreateSub(c1, c2, "c1-c2");
  Value *switch_control = Builder.CreateLoad(switch_control_ptr, "switch_control");

  // if c1 - c2 > gap then we change switch_control to jump to BB, otherwise,
  // jump to BBOpt
  Value *gap_cmp = Builder.CreateICmpSGE(sub, gap, "gap.cmp");
  Value *new_target = Builder.CreateSelect(gap_cmp, BB_target_value, BBOpt_target_value);

  // decide if it is time to change the switch jump
  Value *iter_cmp = Builder.CreateICmpEQ(c1, n_iter, "iter.cmp");
  Value *n_switch_control =
      Builder.CreateSelect(iter_cmp, new_target, switch_control, "new_switch_control");

  // Save the value
  Builder.CreateStore(n_switch_control, switch_control_ptr);
}

void create_BBControl(Function *F,
                      BasicBlock *BBProfile,
                      Value *switch_control_ptr,
                      Value *c1_ptr,
                      Value *c2_ptr) {
#define N_ITER 1000
#define GAP 501

  auto *I32Ty = Type::getInt32Ty(F->getContext());
  auto *n_iter = ConstantInt::get(I32Ty, N_ITER);
  auto *gap = ConstantInt::get(I32Ty, GAP);

  create_BBControl(F, BBProfile, switch_control_ptr, c1_ptr, c2_ptr, n_iter, gap);
}

// finds all users of @I that are outside @BB
std::vector<Instruction *> find_usages_outside_BB(BasicBlock *BB, Instruction *I) {
  std::vector<Instruction *> v;

  for (User *user : I->users()) {
    Instruction *other = cast<Instruction>(user);
    if (other->getParent() != BB)
      v.push_back(other);
  }

  return v;
}

// There might be cases a instruction @I defined on @BB, be used on another
// basic blocks. and because we insert @BBProfile and @BBOpt, @I stop
// dominates all its uses. In those cases we need to create PHI nodes to fix
// the uses!
void create_phi_nodes(Function *F,
                      BasicBlock *BB,
                      BasicBlock *BBProfile,
                      BasicBlock *BBOpt,
                      ValueToValueMapTy &VMapProfile,
                      ValueToValueMapTy &VMapOpt) {
  BasicBlock *BBPhi = BB->splitBasicBlock(BB->getTerminator(), "BBPhi");
  IRBuilder<> Builder(BBPhi->getFirstNonPHI());

  for (auto &I : *BB) {
    // check if all uses of I are in BB
    auto v = find_usages_outside_BB(BB, &I);

    if (v.size() == 0)
      continue;

    // Create the PHI node
    auto T = I.getType();
    auto name = I.getName();
    PHINode *phi = Builder.CreatePHI(T, 3, name + ".phi");

    Instruction *IProf = cast<Instruction>(VMapProfile[&I]);
    Instruction *IOpt = cast<Instruction>(VMapOpt[&I]);

    phi->addIncoming(&I, BB);
    phi->addIncoming(IProf, BBProfile);
    phi->addIncoming(IOpt, BBOpt);

    // iterate over every inst \in v and replace the target operand by the newly created PHI node
    // the target operand is the Value* that produces the PHI node that came from BB.
    for (Instruction *Inst : v) {
      for (unsigned i = 0; i < Inst->getNumOperands(); i++) {
        Value *op = Inst->getOperand(i);
        if (!isa<Instruction>(op))
          continue;
        if (cast<Instruction>(op) == &I) {
          DEBUG(errs() << "replacing " << *op << " -> " << *phi << "\n");
          Inst->setOperand(i, phi);
        }
      }
    }
  }

  // Change the branch inst to jump straight to BBPhi
  BBProfile->getTerminator()->setSuccessor(0, BBPhi);
  BBOpt->getTerminator()->setSuccessor(0, BBPhi);
}

//
void inner_profile(Function *F, ReachableNodes &rn) {
  // load = LoadInst *ptr
  // arith = op @load @other_inst
  // store @arith, *ptr
  StoreInst *store = rn.get_store();
  LoadInst *load = rn.get_load();
  Instruction *arith = rn.get_arith_inst();

  BasicBlock *BB = store->getParent();

  ValueToValueMapTy VMapOpt;
  BasicBlock *BBOpt = deep_clone(BB, VMapOpt, ".opt", F);

  ValueToValueMapTy VMapProfile;
  BasicBlock *BBProfile = deep_clone(BB, VMapProfile, ".profile", F);

  create_phi_nodes(F, BB, BBProfile, BBOpt, VMapProfile, VMapOpt);

  for (phoenix::Node *node : rn.get_nodeset())
    create_BBOpt(VMapOpt, store, node->getValue(), node->getConstant());

  // Let's profile the store to see if it is silent
  // if load == arith then the store will be silent
  auto p = create_BBProfile(F, BBProfile, cast<Instruction>(VMapProfile[load]),
                            cast<Instruction>(VMapProfile[arith]));

  Value *c1 = p.first;
  Value *c2 = p.second;

  Value *switch_control_ptr = create_switch(F, BB, BBProfile, BBOpt);

  create_BBControl(F, BBProfile, switch_control_ptr, c1, c2);
}

void inner_profile(Function *F, std::vector<ReachableNodes> &reachables) {
  if (reachables.empty())
    return;

  for (ReachableNodes &rn : reachables) {
    NodeSet nodes = rn.get_nodeset();
    if (nodes.size())
      inner_profile(F, rn);
  }
}

};  // end namespace phoenix
