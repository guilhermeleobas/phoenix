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
#include <algorithm>    // std::reverse

#include "../Identify/Geps.h"
#include "NodeSet.h"
#include "ReachableNodes.h"
#include "insertIf.h"
#include "utils.h"

#define DEBUG_TYPE "DAG"

using namespace llvm;

namespace phoenix {

llvm::SmallVector<Instruction *, 10> mark_instructions_to_be_moved(StoreInst *store) {
  std::queue<Instruction *> q;
  llvm::SmallVector<Instruction *, 10> marked;

  for (Value *v : store->operands()) {
    if (Instruction *i = dyn_cast<Instruction>(v)) {
      q.push(i);
    }
  }

  marked.push_back(store);

  while (!q.empty()) {
    Instruction *v = q.front();
    q.pop();

    std::for_each(marked.begin(), marked.end(),
                  [](Value *v) { DEBUG(errs() << "mark: " << *v << "\n"); });

    // Check if *v is only used in instructions already marked
    bool all_marked = std::all_of(begin(v->users()), end(v->users()), [&v, &marked](Value *user) {
      if (cast<Instruction>(user)->getParent() != v->getParent())
        return false;
      return find(marked, user) != marked.end();
    });

    if (!all_marked) {
      DEBUG(dbgs() << "-> Ignoring: " << *v << "\n");
      continue;
    }

    // Insert v in the list of marked values and its operands in the queue
    marked.push_back(v);

    if (User *u = dyn_cast<User>(v)) {
      for (Value *op : u->operands()) {
        if (Instruction *inst = dyn_cast<Instruction>(op)) {
          // restrict ourselves to instructions on the same basic block
          if (v->getParent() != inst->getParent()) {
            DEBUG(dbgs() << "-> not in the same BB: " << *inst << "\n");
            continue;
          }

          if (isa<PHINode>(inst))
            continue;

          q.push(inst);
        }
      }
    }
  }

  return marked;
}

void move_marked_to_basic_block(llvm::SmallVector<Instruction *, 10> &marked, Instruction *br) {
  for (Instruction *inst : reverse(marked)) {
    inst->moveBefore(br);
  }
}

void move_from_prev_to_then(BasicBlock *BBPrev, BasicBlock *BBThen) {
  llvm::SmallVector<Instruction *, 10> list;

  for (BasicBlock::reverse_iterator b = BBPrev->rbegin(), e = BBPrev->rend(); b != e; ++b) {
    Instruction *I = &*b;

    if (isa<PHINode>(I) || isa<BranchInst>(I))
      continue;

    if (begin(I->users()) == end(I->users()))
      continue;

    // Move I from BBPrev to BBThen iff all users of I are in BBThen
    //
    // Def 1.
    //  Program P is in SSA form. Therefore, I dominates all its users
    // Def 2.
    //  We are iterating from the end of BBPrev to the beginning. Thus, this
    //  gives us the guarantee that all users of I living in the same BB were
    //  previously visited.

    bool can_move_I = std::all_of(begin(I->users()), end(I->users()), [&BBThen](User *U) {
      BasicBlock *parent = dyn_cast<Instruction>(U)->getParent();
      return (parent == BBThen);
    });

    if (can_move_I) {
      DEBUG(dbgs() << "[BBPrev -> BBThen] " << *I << "\n");
      --b;
      I->moveBefore(BBThen->getFirstNonPHI());
    }
  }
}

void insert_if(StoreInst *store, Value *v, Value *constant) {
  IRBuilder<> Builder(store);

  Value *cmp;

  errs() << "[" << store->getFunction()->getName() << "]: "
         << "inserting if on: " << *v << " with constant: " << *constant << "\n";

  if (v->getType()->isFloatingPointTy()) {
    cmp = Builder.CreateFCmpONE(v, constant);
  } else {
    cmp = Builder.CreateICmpNE(v, constant);
  }

  TerminatorInst *br =
      llvm::SplitBlockAndInsertIfThen(cmp, dyn_cast<Instruction>(cmp)->getNextNode(), false);

  BasicBlock *BBThen = br->getParent();
  BasicBlock *BBPrev = BBThen->getSinglePredecessor();
  BasicBlock *BBEnd = BBThen->getSingleSuccessor();

  // cast<BranchInst>(BBPrev->getTerminator())->swapSuccessors();

  store->moveBefore(br);

  llvm::SmallVector<Instruction *, 10> marked = mark_instructions_to_be_moved(store);

  for_each(marked, [](Instruction *inst) { DEBUG(dbgs() << " Marked: " << *inst << "\n"); });

  move_marked_to_basic_block(marked, br);

  move_from_prev_to_then(BBPrev, BBThen);
  // add_dump_msg(BBThen, "BBThen\n");
  // add_dump_msg(BBEnd, "BBEnd\n");
}

void insert_on_store(Function *F, ReachableNodes &rn) {
  StoreInst *store = rn.get_store();
  LoadInst *load = rn.get_load();
  Instruction *arith = rn.get_arith_inst();
  // @load = Load *ptr
  // @arith = @load op @other
  // Store @arith, *ptr
  // the store is silent if @arith == @load
  insert_if(store, arith, load);
}

void silent_store_elimination(Function *F, std::vector<ReachableNodes> &reachables) {
  for (ReachableNodes &rn : reachables) {
    insert_on_store(F, rn);
  }
}

void load_elimination(Function *F, StoreInst *store, NodeSet &s) {
  for (auto it = s.rbegin(); it != s.rend(); it++) {
    phoenix::Node *node = *it;
    Value *value = node->getValue();
    Value *constant = node->getConstant();
    insert_if(store, value, constant);
    break;
  }
}

void load_elimination(Function *F, std::vector<ReachableNodes> &reachables) {
  for (ReachableNodes &r : reachables) {
    NodeSet nodes = r.get_nodeset();
    load_elimination(F, r.get_store(), nodes);
  }
}

};  // end namespace phoenix
