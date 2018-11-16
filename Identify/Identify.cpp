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
#include "llvm/Transforms/Scalar/EarlyCSE.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include <fstream>
#include <iostream>
#include <set>
#include <stack>

using std::set;
using std::stack;

#include "Identify.h"

#define DEBUG_TYPE "Identify"

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

//

Optional<StoreInst *> Identify::can_reach_store(Instruction *I) {
  assert(is_arith_inst_of_interest(I));

  for (Value *op : I->users()) {
    if (StoreInst *si = dyn_cast<StoreInst>(op)) {
      if (I->getParent() == si->getParent()) // The store should be in the same basic block
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
// std::vector<LoadInst *> Identify::find_load_inst(Instruction *I, Value *v) {
//   assert(isa<Instruction>(v));

//   Instruction *target = dyn_cast<Instruction>(v);

//   if (I->getParent() != target->getParent()) {
//     return std::vector<LoadInst *>();
//   }

//   stack<Instruction *> s;
//   s.push(target);

//   set<Instruction *> visited;
//   std::vector<LoadInst *> vec;

//   while (!s.empty()) {
//     Instruction *aux = s.top();
//     s.pop();

//     if (LoadInst *load = dyn_cast<LoadInst>(aux)) {
//       vec.push_back(load);
//       continue;
//     }

//     for (unsigned i = 0; i < aux->getNumOperands(); ++i) {
//       if (Instruction *other = dyn_cast<Instruction>(aux->getOperand(i))) {
//         // Check if we are still in the same basic block
//         // and if we already visited `other`
//         if (other->getParent() != I->getParent() ||
//             visited.find(other) != visited.end())
//           continue;

//         visited.insert(other);

//         s.push(other);
//       }
//     }
//   }

//   return vec;
// }

Optional<Geps> Identify::good_to_go(Instruction *I) {
  // Given I as:
  //  I: %p_new = op %p_old, %v
  //
  // There are 5 conditions that should be met in order for a match to happen:
  //  1. `I` should be an arithmetic instruction of interest
  //
  //  2. %p_new MUST be used in a store:
  //    store %p_new, %ptr
  //
  //  3. %v MUST be a LoadInst. Otherwise, one can have a case like the one above:
  //      *C = *C x alpha + *B x beta
  //    Even if *B x beta is 0, we can't optimize this instruction because *C x alpha already
  //    changes the value of *C. (Ref. symm.c)
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
  //
  //  Tip: -early-cse makes the analysis easier because it remove redundant computations

  // Check 1
  if (!is_arith_inst_of_interest(I))
    return None;

  // Check 2
  Optional<StoreInst *> store = can_reach_store(I);
  if (!store)
    return None;

  // To-Do: Check for other types? I know that %ptr can be a global variable
  if (!isa<GetElementPtrInst>((*store)->getPointerOperand()))
    return None;

  GetElementPtrInst *dest_gep =
      dyn_cast<GetElementPtrInst>((*store)->getPointerOperand());

  // Perform a check on both operands
  for (unsigned num_op = 0; num_op < 2; ++num_op) {

    // Check 3
    if (!isa<LoadInst>(I->getOperand(num_op)))
      continue;

    LoadInst *load = dyn_cast<LoadInst>(I->getOperand(num_op));

    // Check 4: Same basic block
    if (load->getParent() != I->getParent())
      continue;

    // Check 5:
    GetElementPtrInst *dest_gep =
        dyn_cast<GetElementPtrInst>((*store)->getPointerOperand());
    if (Optional<GetElementPtrInst *> op_gep = check_op(load, dest_gep)) {
      return Geps(dest_gep, *op_gep, *store, I, num_op + 1);
    }

    // std::vector<LoadInst *> loads = find_load_inst(I, I->getOperand(num_op));

    // for (LoadInst *load : loads) {
    //   if (Optional<GetElementPtrInst *> op_gep = check_op(load, dest_gep)) {
    //     return Geps(dest_gep, *op_gep, *si, I, num_op + 1);
    //   }
    // }
  }

  return None;
}

//

llvm::SmallVector<Geps, 10> Identify::get_instructions_of_interest() {
  return instructions_of_interest;
}

// Gather information about I
// unsigned Identify::calc_loop_depth(Instruction *I) {
//   for (LoopInfo::iterator LIT = LI.begin(); LIT != LI.end(); ++LIT) {
//     Loop* ll = *LIT;  
//     ll->dump();
//   }
//   return 0;
// }

void Identify::set_loop_depth(LoopInfo *LI, Geps &g){
  BasicBlock *BB = g.get_instruction()->getParent();
  unsigned depth = LI->getLoopDepth(BB);
  g.set_loop_depth(depth);
}

bool Identify::runOnFunction(Function &F) {

  // Grab loop info
  LoopInfo *LI = &getAnalysis<LoopInfoWrapperPass>().getLoopInfo();

  instructions_of_interest.clear();

  for (auto &BB : F) {
    for (auto &I : BB) {
      if (Optional<Geps> g = good_to_go(&I)) {
        instructions_of_interest.push_back(*g);
      }
    }
  }

  for(Geps &g : instructions_of_interest)
    set_loop_depth(LI, g);

  for (const Geps g : instructions_of_interest) {
    const Instruction *I = g.get_instruction();
    const DebugLoc &loc = I->getDebugLoc();
    if (loc) {
      auto *Scope = cast<DIScope>(loc.getScope());
      DEBUG(dbgs() << *I << " [" << Scope->getFilename() << ":" << loc.getLine()
                   << "]"
                   << "\n");
    }
  }

  return false;
}

void Identify::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<LoopInfoWrapperPass>();
  AU.setPreservesAll();
}

char Identify::ID = 0;
static RegisterPass<Identify> X("Identify", "Find pattern *p = *p `op` v");
