#include "ProgramSlicing.h"

#include "../PDG/PDGAnalysis.h"
#include "llvm/Analysis/LoopInfo.h"

#include "llvm/CodeGen/UnreachableBlockElim.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Transforms/Scalar/DCE.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/LoopUtils.h"

#include <iterator>
#include <queue>
#include <set>

namespace phoenix {

void DetatchDeadBlocks(
     ArrayRef<BasicBlock *> BBs,
     SmallVectorImpl<DominatorTree::UpdateType> *Updates,
     bool KeepOneInputPHIs) {
   for (auto *BB : BBs) {
     // Loop through all of our successors and make sure they know that one
     // of their predecessors is going away.
     SmallPtrSet<BasicBlock *, 4> UniqueSuccessors;
     for (BasicBlock *Succ : successors(BB)) {
       Succ->removePredecessor(BB, KeepOneInputPHIs);
       if (Updates && UniqueSuccessors.insert(Succ).second)
         Updates->push_back({DominatorTree::Delete, BB, Succ});
     }
 
     // Zap all the instructions in the block.
     while (!BB->empty()) {
       Instruction &I = BB->back();
       // If this instruction is used, replace uses with an arbitrary value.
       // Because control flow can't get here, we don't care what we replace the
       // value with.  Note that since this block is unreachable, and all values
       // contained within it must dominate their uses, that all uses will
       // eventually be removed (they are themselves dead).
       if (!I.use_empty())
         I.replaceAllUsesWith(UndefValue::get(I.getType()));
       BB->getInstList().pop_back();
     }
     new UnreachableInst(BB->getContext(), BB);
     assert(BB->getInstList().size() == 1 &&
            isa<UnreachableInst>(BB->getTerminator()) &&
            "The successor list of BB isn't empty before "
            "applying corresponding DTU updates.");
   }
 }

void DeleteDeadBlocks(ArrayRef<BasicBlock *> BBs, bool KeepOneInputPHIs) {
#ifndef NDEBUG
  // Make sure that all predecessors of each dead block is also dead.
  SmallPtrSet<BasicBlock *, 4> Dead(BBs.begin(), BBs.end());
  assert(Dead.size() == BBs.size() && "Duplicating blocks?");
  for (auto *BB : Dead)
    for (BasicBlock *Pred : predecessors(BB))
      assert(Dead.count(Pred) && "All predecessors must be dead!");
#endif

  SmallVector<DominatorTree::UpdateType, 4> Updates;
  DetatchDeadBlocks(BBs, nullptr, KeepOneInputPHIs);


  for (BasicBlock *BB : BBs)
    BB->eraseFromParent();
}

bool EliminateUnreachableBlocks(Function &F, bool KeepOneInputPHIs=false) {
  df_iterator_default_set<BasicBlock *> Reachable;

  // Mark all reachable blocks.
  for (BasicBlock *BB : depth_first_ext(&F, Reachable))
    (void)BB /* Mark all reachable blocks */;

  // Collect all dead blocks.
  std::vector<BasicBlock *> DeadBlocks;
  for (Function::iterator I = F.begin(), E = F.end(); I != E; ++I)
    if (!Reachable.count(&*I)) {
      BasicBlock *BB = &*I;
      DeadBlocks.push_back(BB);
    }

  // Delete the dead blocks.
  DeleteDeadBlocks(DeadBlocks, KeepOneInputPHIs);

  return !DeadBlocks.empty();
}

void ProgramSlicing::set_entry_block(Function *F, LoopInfo &LI, Loop *L) {

  BasicBlock *preheader = L->getLoopPreheader();
  BasicBlock *entry = &F->getEntryBlock();
  if (entry == preheader)
    return;

  // Walk on the parent chain changing the conditional branchs to a direct branch
  // until we reach *entry
  BasicBlock *child = preheader;
  while (child != entry){
    // if the child belongs to a loop, the parent is the loop preheader;
    // otherwise, the parent is the loop predecessor
    BasicBlock *parent = nullptr;

    if (LI.isLoopHeader(child))
      parent = LI.getLoopFor(child)->getLoopPreheader();
    else
      parent = child->getUniquePredecessor();

    assert(parent && "parent == nullptr");

    errs() << "child: " << child->getName() << "\n";
    errs() << "parent: " << parent->getName() << "\n"; 
    connect_basic_blocks(parent, child);
    child = parent;
  }

}

void ProgramSlicing::set_exit_block(Function *F, Loop *L) {
  BasicBlock *L_exit = L->getExitBlock();
  assert (L_exit != nullptr && "L_exit is nullptr");
  BasicBlock *exit = BasicBlock::Create(F->getContext(), "function_exit", F, L_exit);

  IRBuilder<> Builder(exit);
  Builder.CreateRetVoid();

  Instruction *branch = L_exit->getTerminator();

  Builder.SetInsertPoint(branch);
  Builder.CreateBr(exit);

  branch->dropAllReferences();
  branch->eraseFromParent();
}

void ProgramSlicing::connect_basic_blocks(BasicBlock *to, BasicBlock *from){
  Instruction *term = to->getTerminator();
  IRBuilder<> Builder(term);
  Builder.CreateBr(from);
  term->dropAllReferences();
  term->eraseFromParent();
}

void ProgramSlicing::connect_body_to_latch(BasicBlock *body, BasicBlock *latch){
  connect_basic_blocks(body, latch);
}

void ProgramSlicing::connect_header_to_body(Loop *L, BasicBlock *body){
  BasicBlock *header = L->getHeader();
  BasicBlock *exit = L->getExitBlock();

  Instruction *term = header->getTerminator();
  assert(isa<BranchInst>(term) && "term is not a branch inst");
  BranchInst *br = cast<BranchInst>(term);

  // assert(br->getNumSuccessors() == 2 && "branch instruction has more/less than 2 successors");

  if (br->getNumSuccessors() == 2){
    if (br->getSuccessor(0) != exit){
      br->setSuccessor(0, body);
    }
    else {
      br->setSuccessor(1, body);
    }
  }
}

Loop *ProgramSlicing::remove_loops_outside_chain(Loop *parent, Loop *child) {
  if (parent == nullptr){
    return child;
  }

  for (Loop *sub : parent->getSubLoops()){
    if (sub == child)
      continue;

    // connect the subloop preheader to its exit
    connect_basic_blocks(sub->getLoopPreheader(), sub->getExitBlock());
  }

  // Connect the child exit block to the parent latch block
  BasicBlock *exit = child->getExitBlock();
  BasicBlock *latch = parent->getLoopLatch();

  assert (exit != nullptr && latch != nullptr && "exit or parent is null\n");

  Instruction *term = exit->getTerminator();
  IRBuilder<> Builder(term);

  Builder.CreateBr(latch);
  term->dropAllReferences();
  term->eraseFromParent();

  return remove_loops_outside_chain(parent->getParentLoop(), parent);
}

Loop *ProgramSlicing::remove_loops_outside_chain(LoopInfo &LI, BasicBlock *BB) {
  Loop *L = LI.getLoopFor(BB);
  if (L == nullptr)
    return nullptr;

  // connect header to body
  connect_header_to_body(L, BB);
  // connect body to latch
  connect_body_to_latch(BB, L->getLoopLatch());

  Loop *outer = remove_loops_outside_chain(L->getParentLoop(), L);

  return outer;
}

// Returns the number of users of a given instruction
unsigned get_num_users(Instruction *I) {
  unsigned nUsers = 0;

  for (User *u : I->users())
    ++nUsers;

  return nUsers;
}

void ProgramSlicing::slice(Function *F, Instruction *I) {
  errs() << "[INFO]: Applying slicing on: " << F->getName() << "\n";

  DominatorTree DT(*F);
  PostDominatorTree PDT;
  PDT.recalculate(*F);
  LoopInfo LI(DT);

  Loop *L = remove_loops_outside_chain(LI, I->getParent());
  assert (L != nullptr && "Loop is null!");

  set_entry_block(F, LI, L);
  set_exit_block(F, L);

  EliminateUnreachableBlocks(*F);

  PassBuilder PB;
  FunctionAnalysisManager FAM;
  PB.registerFunctionAnalyses(FAM);

  UnreachableBlockElimPass u;
  u.run(*F, FAM);

  DCEPass d;
  d.run(*F, FAM);

  ProgramDependenceGraph PDG;
  PDG.compute_dependences(F);
  std::set<Instruction*> dependences = PDG.get_dependences_for(I);

  std::queue<Instruction *> q;
  for (Instruction &I : instructions(F)) {
    if (isa<BranchInst>(&I) || isa<ReturnInst>(&I) || dependences.find(&I) != dependences.end())
      continue;

    I.dropAllReferences();
    q.push(&I);
    // I.eraseFromParent();
  }

  while (!q.empty()) {
    Instruction *I = q.front();
    q.pop();
    I->eraseFromParent();
  }

  // F->viewCFG();
}

}  // namespace phoenix
