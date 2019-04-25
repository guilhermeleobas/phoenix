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

#define SWITCH_NUM_CASES 3
#define SWITCH_BB_PROFILE_INDEX 0
#define SWITCH_BB_INDEX 1
#define SWITCH_BB_OPT_INDEX 2

Instruction *DAG::create_switch(Function *F, BasicBlock *BB,
                                BasicBlock *BBProfile, BasicBlock *BBOpt) {
  // 1. Create a variable to control the switch
  IRBuilder<> Builder(F->getEntryBlock().getFirstNonPHI());
  Instruction *switch_control_ptr = Builder.CreateAlloca(
      Type::getInt32Ty(F->getContext()), nullptr, "switch.control_ptr");
  Builder.CreateStore(ConstantInt::get(Type::getInt32Ty(F->getContext()),
                                       SWITCH_BB_PROFILE_INDEX),
                      switch_control_ptr);

  // 2. Create the switch with a default jump to BBProfile
  BasicBlock *BBSwitch =
      BasicBlock::Create(F->getContext(), "Switch", F, BB);
  Builder.SetInsertPoint(BBSwitch);
  Instruction *load = Builder.CreateLoad(switch_control_ptr, "switch_control");
  SwitchInst *si = Builder.CreateSwitch(load, BBProfile, SWITCH_NUM_CASES);

  // Case with 1 with a jump to BB
  si->addCase(
      ConstantInt::get(Type::getInt32Ty(F->getContext()), SWITCH_BB_INDEX), BB);

  // Case 2 with a jump to the optimized BB
  si->addCase(
      ConstantInt::get(Type::getInt32Ty(F->getContext()), SWITCH_BB_OPT_INDEX),
      BBOpt);

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

  return switch_control_ptr;
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
AllocaInst *DAG::create_c1(Function *F, BasicBlock *BBProfile) {
  IRBuilder<> Builder(F->getEntryBlock().getFirstNonPHI());

  auto *I32Ty = Type::getInt32Ty(F->getContext());
  auto *zero = ConstantInt::get(I32Ty, 0);
  auto *one = ConstantInt::get(I32Ty, 1);

  AllocaInst *c1_ptr =
      Builder.CreateAlloca(I32Ty, nullptr, "c1_ptr." + BBProfile->getName());
  Builder.CreateStore(zero, c1_ptr);

  // c1 inc
  Builder.SetInsertPoint(BBProfile->getFirstNonPHI());

  LoadInst *load_c1 = Builder.CreateLoad(c1_ptr, "c1.load");
  Value *c1_inc = Builder.CreateAdd(load_c1, one, "c1.inc");
  Builder.CreateStore(c1_inc, c1_ptr);

  return c1_ptr;
}

// Create and increment c2 when V == constraint
AllocaInst *DAG::create_c2(Function *F, BasicBlock *BBProfile, Value *V,
                           Value *constraint) {
  IRBuilder<> Builder(F->getEntryBlock().getFirstNonPHI());

  auto *I32Ty = Type::getInt32Ty(F->getContext());
  auto *zero = ConstantInt::get(I32Ty, 0);
  auto *one = ConstantInt::get(I32Ty, 1);

  AllocaInst *c2_ptr =
      Builder.CreateAlloca(I32Ty, nullptr, "c2_ptr." + BBProfile->getName());
  Builder.CreateStore(zero, c2_ptr);

  // c2 inc
  Builder.SetInsertPoint(cast<Instruction>(V)->getNextNode());

  Value *cmp;
  if (V->getType()->isFloatingPointTy())
    cmp = Builder.CreateFCmpOEQ(V, constraint, "cmp.profile");
  else
    cmp = Builder.CreateICmpEQ(V, constraint, "cmp.profile");

  Value *select = Builder.CreateSelect(cmp, zero, one);

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
std::pair<Value*, Value*> DAG::create_BBProfile(Function *F, ValueToValueMapTy &VMap, BasicBlock *BBProfile, Value *V,
                      Value *constraint) {
  Value *c1 = create_c1(F, BBProfile);
  Value *c2 = create_c2(F, BBProfile, cast<Value>(VMap[V]), constraint);

  return std::make_pair(c1, c2);
}

//
void DAG::create_BBOpt(ValueToValueMapTy &VMap, StoreInst *store, Value *V,
                       Value *constraint) {
  insert_if(cast<StoreInst>(VMap[store]), VMap[V], constraint);
}

// Creates a BasicBlock right after BBProfile that controls the control flow
// from BBProfile to BB or BBOpt
void DAG::create_BBControl(Function *F, BasicBlock *BBProfile,
                           Value *switch_control_ptr, Value *c1_ptr,
                           Value *c2_ptr, ConstantInt *n_iter,
                           ConstantInt *gap) {
  auto *I32Ty = Type::getInt32Ty(F->getContext());
  auto *BBOpt_target_value = ConstantInt::get(I32Ty, SWITCH_BB_OPT_INDEX);
  auto *BB_target_value = ConstantInt::get(I32Ty, SWITCH_BB_INDEX);

  auto *one = ConstantInt::get(I32Ty, 1);
  auto *zero = ConstantInt::get(I32Ty, 0);

  // Create BBControl and insert it right after BBProfile
  BasicBlock *BBControl =
      BBProfile->splitBasicBlock(BBProfile->getTerminator(), "BBControl");
  IRBuilder<> Builder(BBControl->getFirstNonPHI());

  Value *c1 = Builder.CreateLoad(c1_ptr, "c1");
  Value *c2 = Builder.CreateLoad(c2_ptr, "c2");
  Value *sub = Builder.CreateSub(c1, c2, "c1-c2");
  Value *switch_control =
      Builder.CreateLoad(switch_control_ptr, "switch_control");

  // if c1 - c2 > gap then we change switch_control to jump to BB, otherwise,
  // jump to BBOpt
  Value *gap_cmp = Builder.CreateICmpSGE(sub, gap, "gap.cmp");
  Value *new_target =
      Builder.CreateSelect(gap_cmp, BB_target_value, BBOpt_target_value);

  // decide if it is time to change the switch jump
  Value *iter_cmp = Builder.CreateICmpEQ(c1, n_iter, "iter.cmp");
  Value *n_switch_control = Builder.CreateSelect(
      iter_cmp, new_target, switch_control, "new_switch_control");

  // Save the value
  Builder.CreateStore(n_switch_control, switch_control_ptr);
}

void DAG::create_BBControl(Function *F, BasicBlock *BBProfile,
                           Value *switch_control_ptr, Value *c1_ptr,
                           Value *c2_ptr) {
#define N_ITER 1000
#define GAP 501

  auto *I32Ty = Type::getInt32Ty(F->getContext());
  auto *n_iter = ConstantInt::get(I32Ty, N_ITER);
  auto *gap = ConstantInt::get(I32Ty, GAP);

  create_BBControl(F, BBProfile, switch_control_ptr, c1_ptr, c2_ptr, n_iter,
                   gap);
}

std::vector<Instruction *> DAG::find_usages_outside_BB(BasicBlock *BB,
                                                       Instruction *I) {
  std::vector<Instruction *> v;

  for (User *user : I->users()) {
    Instruction *other = cast<Instruction>(user);
    if (other->getParent() != BB)
      v.push_back(other);
  }

  return v;
}

void DAG::create_phi_nodes(Function *F, BasicBlock *BB, BasicBlock *BBProfile,
                           BasicBlock *BBOpt, ValueToValueMapTy &VMapProfile,
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
        if (cast<Instruction>(op) == &I){
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
void DAG::profile_and_optimize(Function *F, const Geps &g,
                               const phoenix::Node *node, bool profile) {


  if (profile) {
    BasicBlock *BB = node->getInst()->getParent();

    ValueToValueMapTy VMapOpt;
    BasicBlock *BBOpt = deep_clone(BB, VMapOpt, ".opt", F);

    ValueToValueMapTy VMapProfile;
    BasicBlock *BBProfile = deep_clone(BB, VMapProfile, ".profile", F);

    assert(node->hasConstraint() && "Node do not have a constraint");

    create_phi_nodes(F, BB, BBProfile, BBOpt, VMapProfile, VMapOpt);

    create_BBOpt(VMapOpt, g.get_store_inst(), node->getValue(),
                 node->getConstraint());

    Value *V = node->getValue();
    Value *constraint = node->getConstraint();

    auto p = create_BBProfile(F, VMapProfile, BBProfile, V, constraint);
    Value *c1 = p.first;
    Value *c2 = p.second;

    Value *switch_control_ptr = create_switch(F, BB, BBProfile, BBOpt);

    create_BBControl(F, BBProfile, switch_control_ptr, c1, c2);
  }
  else {
    StoreInst *store = g.get_store_inst();
    Value *value = node->getValue();
    Value *constraint = node->getConstraint();
    insert_if(g.get_store_inst(), value, constraint);
  }
}


void DAG::split(StoreInst *store) const {
  BasicBlock *BB = store->getParent();
  auto *n = BB->splitBasicBlock(store->getNextNode());
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

    // Split the basic block at each store instruction
    split(g.get_store_inst());

    phoenix::StoreNode *store =
        cast<phoenix::StoreNode>(myParser(g.get_store_inst(), g.get_operand_pos()));

    ConstraintVisitor cv(store, &g);
    DepthVisitor dv(store);

    NodeSet s = dv.getSet();

    for (auto node : s) {
      // DotVisitor dv(store);
      // dv.print();
      errs() << "[" << F.getName() << "] " << *node << "\n";

      profile_and_optimize(&F, g, node, true);

      break;  // Let's just insert on the first element;
    }
    errs() << "\n";
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
