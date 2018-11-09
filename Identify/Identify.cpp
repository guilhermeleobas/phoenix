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
#include "llvm/Support/raw_ostream.h" // For dbgs()
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include <fstream>
#include <iostream>
#include <set>
#include <stack>

using std::set;
using std::stack;

#include "Identify.h"

#define DEBUG_TYPE "Count"

bool Identify::is_arith_inst_of_interest(Instruction *I) {
  switch (I->getOpcode()) {
  case Instruction::Add:
  case Instruction::Sub:
  case Instruction::Mul:
  // case Instruction::UDiv:
  // case Instruction::SDiv:
  // case Instruction::Shl:
  // case Instruction::LShr:
  // case Instruction::AShr:
  // case Instruction::And:
  // case Instruction::Or:
  case Instruction::Xor:
  case Instruction::FAdd:
  case Instruction::FSub:
  case Instruction::FMul:
    return true;
  default:
    return false;
  }
}

Optional<StoreInst *> Identify::can_reach_store(Instruction *I) {
  assert(is_arith_inst_of_interest(I));

  for (Value *op : I->users()) {
    if (StoreInst *si = dyn_cast<StoreInst>(op)) {
      return si;
    }
  }

  return None;
}

// Let's just check if the operands of the two instructiosn are the same
bool Identify::check_operands_equals(const Value *vu, const Value *vv) {
  if (vu == vv)
    return true;

  if (!isa<Instruction>(vu) || !isa<Instruction>(vv))
    return false;

  const Instruction *u = dyn_cast<Instruction>(vu);
  const Instruction *v = dyn_cast<Instruction>(vv);

  if (u->getNumOperands() != v->getNumOperands())
    return false;

  for (unsigned i = 0; i < u->getNumOperands(); i++)
    if (u->getOperand(i) != v->getOperand(i))
      return false;

  // Insane check
  if (u->getType() != v->getType())
    return false;

  return true;
}

// This method checks if op was produced by a LoadInst whose
// the pointer loaded is the same as the dest_gep
// Given that every GetElementPtrInst has a %base pointer as
// as well as an %offset, we just compare them.
Optional<GetElementPtrInst *> Identify::check_op(LoadInst *li,
                                                 GetElementPtrInst *dest_gep) {

  // To-Do, check for types other than GetElementPtrInst
  if (!isa<GetElementPtrInst>(li->getPointerOperand()))
    return None;

  GetElementPtrInst *op_gep =
      dyn_cast<GetElementPtrInst>(li->getPointerOperand());

  // Check 4: Pointers should be from the same basic block
  if (dest_gep->getParent() != op_gep->getParent())
    return None;

  // Check 5: both geps should have the same type
  if (dest_gep->getType() != op_gep->getType())
    return None;

  // errs() << *dest_gep << "\n";
  // errs() << *op_gep << "\n";

  if (dest_gep->getNumOperands() != op_gep->getNumOperands())
    return None;

  // Check the base pointers first
  if (dest_gep->getPointerOperand() != op_gep->getPointerOperand())
    return None;

  for (unsigned i = 1; i < dest_gep->getNumOperands(); i++) {
    if (!check_operands_equals(dest_gep->getOperand(i), op_gep->getOperand(i)))
      return None;
  }

  return op_gep;
}

// Iterate backwards to see if v was produced by a load
// Only iterate on instructions on the same basic block
std::vector<LoadInst *> Identify::find_load_inst(Value *v) {
  assert(isa<Instruction>(v));

  stack<Instruction *> s;
  s.push(dyn_cast<Instruction>(v));

  set<Instruction *> visited;
  std::vector<LoadInst *> vec;

  while (!s.empty()) {
    Instruction *I = s.top();
    s.pop();

    if (LoadInst *load = dyn_cast<LoadInst>(I)) {
      vec.push_back(load);
      continue;
    }

    for (unsigned i = 0; i < I->getNumOperands(); ++i) {
      if (Instruction *other = dyn_cast<Instruction>(I->getOperand(i))) {
        // Check if we are still in the same basic block
        // and if we didn't visited `other`
        if (other->getParent() != I->getParent() ||
            visited.find(other) != visited.end())
          continue;

        visited.insert(other);

        s.push(other);
      }
    }
  }

  return vec;
}

Optional<Geps> Identify::good_to_go(Instruction *I) {
  // Given I as
  //   I: %dest = `op` %a, %b
  //
  // There are 5 conditions that should be met:
  //  1. `I` should be an arithmetic instruction of interest
  //
  //  2. %dest MUST be used in a store:
  //    store %dest, %ptr
  //
  //  3. either %a or %b must be loaded from the same %ptr
  //    ptr = getElementPtr %base, %offset
  //  Both %base and %offset should be the same
  //
  //  4. Both instructions must be on the same basic block!
  //      while (x > 0) {
  //        y = gep p, 0, x
  //      }
  //      ...
  //      z = gep p, 0, x
  //    In the case above, geps are the same but the first one will
  //    not have the same value all the time! Therefore, it's important
  //    that we only check for geps that are only on the same basic block!
  //
  //  5. Both geps should be of the same type!
  //       p = global int
  //       y = gep p, 0, x
  //       z = gep cast p to char*, 0, x
  //     In the case above, both geps will hold diferent values since the first
  //     is a gep for an int* and the second for a char*
  //
  //  Idea: Use RangeAnalysis here to check the offset? Maybe!?
  //  If we use RangeAnalysis, we can drop check 4 when the base pointers are
  //  the same

  // Check 1
  if (!is_arith_inst_of_interest(I))
    return None;

  // Check 2
  Optional<StoreInst *> si = can_reach_store(I);
  if (!si)
    return None;

  // To-Do: Check for other types? I know that %ptr can be a global variable
  if (!isa<GetElementPtrInst>((*si)->getPointerOperand()))
    return None;

  GetElementPtrInst *dest_gep =
      dyn_cast<GetElementPtrInst>((*si)->getPointerOperand());

  // Check 3:
  // Perform a check on both operands
  for (unsigned num_op = 0; num_op < 2; ++num_op) {
    if (!isa<Instruction>(I->getOperand(num_op)))
      continue;
    std::vector<LoadInst *> loads = find_load_inst(I->getOperand(num_op));
    for (LoadInst *load : loads) {
      if (Optional<GetElementPtrInst *> op_gep = check_op(load, dest_gep)) {
        return Geps(dest_gep, *op_gep, *si, load, I, num_op + 1);
      }
    }
  }

  return None;
}

std::vector<Geps> Identify::get_instructions_of_interest() {
  return instructions_of_interest;
}

bool Identify::runOnBasicBlock(BasicBlock &BB) {

  instructions_of_interest.clear();

  for (auto &I : BB) {
    if (Optional<Geps> g = good_to_go(&I)) {
      instructions_of_interest.push_back(*g);
    }
  }

  return false;
}

void Identify::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
}

char Identify::ID = 0;
static RegisterPass<Identify> X("Identify", "Find pattern *p = *p `op` v");
