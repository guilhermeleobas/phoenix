#pragma once

#include "ReachableNodes.h"
#include "NodeSet.h"

#define DEBUG_TYPE "DAG"

using namespace llvm;

namespace phoenix {

llvm::SmallVector<Instruction *, 10> mark_instructions_to_be_moved(
    StoreInst *store);

void move_marked_to_basic_block(
    llvm::SmallVector<Instruction *, 10> &marked, Instruction *br);

void move_from_prev_to_then(BasicBlock *BBPrev, BasicBlock *BBThen);

void insert_if(StoreInst *store, Value *v, Value *constant);

void insert_on_store(Function *F, std::vector<ReachableNodes> &reachables);
void silent_store_elimination(Function *F, std::vector<ReachableNodes> &reachables);

void load_elimination(Function *F, StoreInst *store, NodeSet &s);
void load_elimination(Function *F, std::vector<ReachableNodes> &reachables);

}; // end namespace phoenix