#pragma once

#define DEBUG_TYPE "DAG"

using namespace llvm;

namespace phoenix {

llvm::SmallVector<Instruction *, 10> mark_instructions_to_be_moved(
    StoreInst *store);

void move_marked_to_basic_block(
    llvm::SmallVector<Instruction *, 10> &marked, Instruction *br);

void move_from_prev_to_then(BasicBlock *BBPrev, BasicBlock *BBThen);

void insert_if(StoreInst *store, Value *v, Value *constant);

void no_profile(Function *F, const Geps &g, NodeSet &s);

}; // end namespace phoenix