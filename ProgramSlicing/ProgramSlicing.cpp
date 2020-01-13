#include "ProgramSlicing.h"

#include "../PDG/PDGAnalysis.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Verifier.h"

#include "llvm/CodeGen/UnreachableBlockElim.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Transforms/Scalar/DCE.h"
#include "llvm/Transforms/Scalar/Sink.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include "llvm/Transforms/Scalar/SimplifyCFG.h"

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

    connect_basic_blocks(parent, child);
    child = parent;
  }

}

void ProgramSlicing::set_exit_block(Function *F) {
  BasicBlock *F_exit = nullptr;

  for (BasicBlock &BB : *F){
    if (isa<ReturnInst>(BB.getTerminator()))
      F_exit = &BB;
  }

  assert (F_exit != nullptr && "F_exit is nullptr");
  BasicBlock *exit = BasicBlock::Create(F->getContext(), "function_exit", F, F_exit);

  IRBuilder<> Builder(exit);
  Builder.CreateRetVoid();

  Instruction *ret = F_exit->getTerminator();

  Builder.SetInsertPoint(ret);
  Builder.CreateBr(exit);

  ret->dropAllReferences();
  ret->eraseFromParent();
}

std::set<BasicBlock*> compute_alive_blocks(Function *F, std::set<Instruction*> &dependences){
  std::set<BasicBlock*> alive;

  for (auto &BB : *F){
    for (Instruction &I : BB){
      if (dependences.find(&I) != dependences.end()){
        // errs() << "alive: " << BB.getName() << "\n";
        alive.insert(&BB);
        break;
      }
    }
  }

  return alive;
}

unsigned num_pred(BasicBlock *BB){
  unsigned i = 0;
  for (auto it = pred_begin(BB); it != pred_end(BB); ++it)
    ++i;
  return i;
}

unsigned num_succ(BasicBlock *BB){
  unsigned i = 0;
  for (auto it = succ_begin(BB); it != succ_end(BB); ++it)
    ++i;
  return i;
}

void connect_indirect_jump(BasicBlock *pred, BasicBlock *succ){
  // errs() << "Connecting: " << pred->getName() << " -> " << succ->getName();
  BranchInst *term = cast<BranchInst>(pred->getTerminator());
  assert(term->getNumSuccessors() == 1);
  term->setSuccessor(0, succ);
}

// Pred contains a conditional jump
void connect_cond_jump(BasicBlock *pred, BasicBlock *BB, BasicBlock *succ){
  BranchInst *term = cast<BranchInst>(pred->getTerminator());
  for (unsigned i = 0; i < term->getNumSuccessors(); i++){
    if (term->getSuccessor(i) == BB){
      // errs() << "Setting successor of : " << pred->getName() << " -> " << succ->getName() << "\n";
      term->setSuccessor(i, succ);
      return;
    }
  }
}

void connect_pred_to_succ(BasicBlock *pred, BasicBlock *BB, BasicBlock *succ){
  /*
    @pred -> @BB -> @succ
  */
  // errs() << "Connecting: " << pred->getName() << " to " << succ->getName() << "\n";
  if (num_succ(pred) == 1)
    connect_indirect_jump(pred, succ);
  else
    connect_cond_jump(pred, BB, succ);

}

bool conditional_to_direct(BasicBlock *BB){
  /*
    If BB contains a instruction of the form:
     br T %val, label %BB, label %succ 
    We replace it by a direct jump:
     br label %succ 
  */

  if (BB->empty()
      || BB->getTerminator() == nullptr)
    return false;

  if (!isa<BranchInst>(BB->getTerminator()))
    return false;

  BranchInst *term = cast<BranchInst>(BB->getTerminator());

  if (term->isUnconditional())
    return false;

  BasicBlock *succ = nullptr;

  if (term->getSuccessor(0) == BB)
    succ = term->getSuccessor(1);
  else if (term->getSuccessor(1) == BB)
    succ = term->getSuccessor(0);

  if (succ == nullptr)
    return false;

  BranchInst *rep = BranchInst::Create(succ, term);
  term->dropAllReferences();
  term->eraseFromParent();
  return true;
}

bool fix_conditional_jump_to_same_block(BasicBlock *BB){
  /*
    If BB contains a instruction of the form:
     br T %val, label %succ, label %succ 
    We replace it by a direct jump:
     br label %succ 
  */

  if (BB->empty()
      || BB->getTerminator() == nullptr)
    return false;

  if (!isa<BranchInst>(BB->getTerminator()))
    return false;

  BranchInst *term = cast<BranchInst>(BB->getTerminator());

  if (term->isUnconditional())
    return false;

  if (term->getSuccessor(0) == term->getSuccessor(1)){
    BranchInst *rep = BranchInst::Create(term->getSuccessor(0), term);
    term->dropAllReferences();
    term->eraseFromParent();
  }

  return true;
}

void delete_branch(BasicBlock *BB){
  assert(isa<BranchInst>(BB->getTerminator()));
  BranchInst *term = cast<BranchInst>(BB->getTerminator());
  term->dropAllReferences();
  term->eraseFromParent();
}

bool remove_successor(BasicBlock *BB, BasicBlock *succ){
  if (!isa<BranchInst>(BB->getTerminator()))
    return false;

  BranchInst *term = cast<BranchInst>(BB->getTerminator());

  if (term->isUnconditional()){
    delete_branch(BB);
  }
  else {
    unsigned idx = (term->getSuccessor(0) == succ) ? 1 : 0;
    BasicBlock *other = term->getSuccessor(idx);
    term->setSuccessor((idx+1)%2, other);
    fix_conditional_jump_to_same_block(BB);
  }

  return true;
}

void fix_phi_nodes(BasicBlock *prev, BasicBlock *BB, BasicBlock *succ) {

  auto *Old = BB;
  auto *New = prev;
  for (PHINode &phi : succ->phis()){
    for (unsigned Op = 0, NumOps = phi.getNumOperands(); Op != NumOps; ++Op)
      if (phi.getIncomingBlock(Op) == Old)
        phi.setIncomingBlock(Op, New);
  }

}

std::vector<BasicBlock*> collect_predecessors(BasicBlock *BB){
  std::vector<BasicBlock*> v;
  for (BasicBlock *pred : predecessors(BB)){
    v.push_back(pred);
  } 
  return v;
}

void erase_block(BasicBlock *BB){
  if (!BB->empty()){
    std::queue<Instruction*> q;
    for (Instruction &I : *BB){
      I.dropAllReferences();
      I.replaceAllUsesWith(UndefValue::get(I.getType()));
      q.push(&I);
    }

    while (!q.empty()) {
      Instruction *I = q.front();
      q.pop();
      I->eraseFromParent();
    }
  } 
  BB->dropAllReferences();
  BB->eraseFromParent();
}

bool merge_return_blocks(Function *F){

  std::vector<BasicBlock*> v;

  for (BasicBlock &BB : *F) {
    if (!BB.empty()
        && isa<ReturnInst>(BB.getTerminator()))
      v.push_back(&BB);
  }

  if (v.size() == 1)
    return false;

  BasicBlock *ret = BasicBlock::Create(F->getContext(), "function_exit", F, nullptr);
  auto *ri = ReturnInst::Create(F->getContext(), nullptr, ret);

  for (BasicBlock *BB : v){
    auto *term = BB->getTerminator();
    BranchInst *br = BranchInst::Create(ret, term);
    term->dropAllReferences();
    term->eraseFromParent();
  }

  return true;
}

void delete_block(BasicBlock *BB){
  /*
    1. If the block has a single predecessor/successor, connect them
  */

  // errs() << "deleting block: " << BB->getName() << "\n\n";
  Function *F = BB->getParent();

  conditional_to_direct(BB);
  fix_conditional_jump_to_same_block(BB);

  if (num_succ(BB) == 0){
    // BB->getParent()->viewCFG();
    auto preds = collect_predecessors(BB);
    for (auto *pred : preds){
      remove_successor(pred, BB);
    }
    // BB->getParent()->viewCFG();
  }
  else if (num_pred(BB) == 1 and num_succ(BB) == 1){
    auto *pred = BB->getSinglePredecessor();
    auto *succ = BB->getSingleSuccessor();
    if (pred != succ){
      connect_pred_to_succ(pred, BB, succ);
      fix_phi_nodes(pred, BB, succ);
    }
    else {
      // pred -> BB 
      //   ^      |
      //   |______|
      // errs() << "Disconnecting " << pred->getName() << " -> " << BB->getName() << "\n";
      remove_successor(pred, BB);
    }
  } 
  else if (num_pred(BB) > 1 and num_succ(BB) == 1){
    auto preds = collect_predecessors(BB);

    for (BasicBlock *pred : preds){
      auto *succ = BB->getSingleSuccessor();
      connect_pred_to_succ(pred, BB, succ);
      fix_phi_nodes(pred, BB, succ);
    }
  }
  else if (num_pred(BB) >= 1 and num_succ(BB) > 1){
    assert("both #predecessors and #successors are greater than 1");
  }

  erase_block(BB);
}

std::vector<BasicBlock*> get_dead_blocks(Function *F, std::set<BasicBlock*> &alive_blocks){
  df_iterator_default_set<BasicBlock *> Reachable;
  std::vector<BasicBlock*> d;

  for (BasicBlock *BB : depth_first_ext(F, Reachable)){
    if (alive_blocks.find(BB) == alive_blocks.end()){
      d.push_back(BB);
    }
  }

  std::reverse(d.begin(), d.end());

  return d;
}

bool can_delete_block(Function *F, BasicBlock *BB){
  // if (&F->getEntryBlock() == BB)
  if (&F->getEntryBlock() == BB || isa<ReturnInst>(BB->getTerminator()))
    return false;
  return true;
}

void delete_blocks(Function *F, std::set<BasicBlock*> &alive_blocks){
  merge_return_blocks(F);
  auto dead_blocks = get_dead_blocks(F, alive_blocks);

  FunctionAnalysisManager FAM;
  SinkingPass si;

  for (auto *BB : dead_blocks){
    if (can_delete_block(F, BB)){
      delete_block(BB);
    }
  }
}

void delete_empty_blocks(Function *F){
  std::queue<BasicBlock*> q;
  for (auto &BB : *F){
    if (BB.empty()){
      assert(num_pred(&BB) == 1 && "Basic Block has more than one predecessor here!");
      remove_successor(BB.getSinglePredecessor(), &BB);
      q.push(&BB);
    }
  }

  while (!q.empty()){
    auto *BB = q.front();
    BB->eraseFromParent();
    q.pop();
  }
}

void ProgramSlicing::slice(Function *F, Instruction *I) {
  errs() << "[INFO]: Applying slicing on: " << F->getName() << "\n";

  DominatorTree DT(*F);
  PostDominatorTree PDT;
  PDT.recalculate(*F);
  LoopInfo LI(DT);

  set_exit_block(F);

  PassBuilder PB;
  FunctionAnalysisManager FAM;
  PB.registerFunctionAnalyses(FAM);

  UnreachableBlockElimPass u;
  u.run(*F, FAM);

  DCEPass d;
  d.run(*F, FAM);

  SimplifyCFGPass sf;
  sf.run(*F, FAM);

  SinkingPass si;
  si.run(*F, FAM);

  ProgramDependenceGraph PDG;
  PDG.compute_dependences(F);
  std::set<Instruction*> dependences = PDG.get_dependences_for(I);
  // PDG.get_dependence_graph()->to_dot();

  std::set<BasicBlock*> alive_blocks = compute_alive_blocks(F, dependences);

  std::queue<Instruction *> q;

  for (Instruction &I : instructions(F)) {
    if (isa<BranchInst>(&I) || isa<ReturnInst>(&I) || dependences.find(&I) != dependences.end())
      continue;

    I.dropAllReferences();
    I.replaceAllUsesWith(UndefValue::get(I.getType()));
    q.push(&I);
  }

  while (!q.empty()) {
    Instruction *I = q.front();
    q.pop();
    I->eraseFromParent();
  }

  delete_empty_blocks(F);

  alive_blocks = compute_alive_blocks(F, dependences);

  u.run(*F, FAM);
  si.run(*F, FAM);
  sf.run(*F, FAM);


  delete_blocks(F, alive_blocks);

  u.run(*F, FAM);
  // sf.run(*F, FAM);

  // VerifierPass ver;
  // ver.run(*F, FAM);

}

}  // namespace phoenix
