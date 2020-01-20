#include "ProgramSlicing.h"

#include "../PDG/PDGAnalysis.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/RegionInfo.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/DominanceFrontier.h"
#include "llvm/IR/Verifier.h"

#include "llvm/CodeGen/UnreachableBlockElim.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Transforms/Scalar/DCE.h"
#include "llvm/Transforms/Scalar/Sink.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include "llvm/Transforms/Scalar/SimplifyCFG.h"

#include "llvm/Support/GraphWriter.h"

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

bool replace_jump_by_selfedge(BasicBlock *curr){
  if (!curr->size())
    return false;

  assert(curr->size() == 1);

  BranchInst *term = cast<BranchInst>(curr->getTerminator());
  BranchInst *rep = BranchInst::Create(curr, term);
  term->dropAllReferences();
  term->eraseFromParent();

  return true;
}

/*
  ...
*/
void replace_direct_jump(BasicBlock *pred, BasicBlock *postdom){
  // errs() << "Connecting: " << pred->getName() << " -> " << succ->getName();
  BranchInst *term = cast<BranchInst>(pred->getTerminator());
  assert(term->getNumSuccessors() == 1);
  term->setSuccessor(0, postdom);
}

/*
      pred
      /   \ 
     /     \
    curr  other
      |     |
     ...   ...
      |
     pdom
*/
void replace_conditional_jump(BasicBlock *pred, BasicBlock *curr, BasicBlock *postdom){
  BranchInst *term = cast<BranchInst>(pred->getTerminator());
  for (unsigned i = 0; i < term->getNumSuccessors(); i++){
    if (term->getSuccessor(i) == curr){
      // errs() << "Setting successor of : " << pred->getName() << " -> " << succ->getName() << "\n";
      term->setSuccessor(i, postdom);
      return;
    }
  }
}

/*
  Remove the edge from @pred to @curr and create a new one from @pred to @postdom
*/
void connect_pred_to_postdom(BasicBlock *pred, BasicBlock *curr, BasicBlock *postdom){
  // errs() << "Connecting: " << pred->getName() << " to " << postdom->getName() << "\n";

  if (num_succ(pred) == 1)
    replace_direct_jump(pred, postdom);
  else
    replace_conditional_jump(pred, curr, postdom);
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

std::vector<BasicBlock*> get_predecessors(BasicBlock *BB){
  std::vector<BasicBlock*> v;
  for (BasicBlock *pred : predecessors(BB)){
    v.push_back(pred);
  } 
  return v;
}

void erase_block(PostDominatorTree &PDT, BasicBlock *BB){
  errs() << "Deleting block: " << *BB << "\n\n";
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

std::vector<BasicBlock*> compute_dead_blocks(Function *F, std::set<BasicBlock*> &alive_blocks){
  df_iterator_default_set<BasicBlock *> Reachable;
  std::vector<BasicBlock*> d;

  for (BasicBlock *BB : depth_first_ext(F, Reachable)){
    if (!BB->empty() && isa<ReturnInst>(BB->getTerminator()))
      continue;
    if (alive_blocks.find(BB) == alive_blocks.end()){
      d.push_back(BB);
    } 
  }

  std::reverse(d.begin(), d.end());

  return d;
}

bool can_delete_block(Function *F, BasicBlock *BB){
  if (&F->getEntryBlock() == BB || isa<ReturnInst>(BB->getTerminator()))
    return false;
  return true;
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

/*
  We split the live of each predicate that is defined in a different basic block

  %Foo
    %cmp = ...
    %cond = ...
    br i1 %cond, label %Bar, label %Baz

  %Bar
    br i1 %cmp, label %Baz, label %Foo 

  # Here, %cmp is used but it is defined on %Foo. To circumvent this case
  # we split the live of %cmp with a PHI node:

  %Bar2
    %cmp_PHI = PHI [%cmp, %Foo]
    br i1 %cmp_PHI, label %Baz, label %Foo 
*/
void split_predicate_live(Function *F){
  for (BasicBlock &BB : *F) {

    // Only interested in branch insts
    if (BB.empty() || !isa<BranchInst>(BB.getTerminator()))
      continue;

    // Branch must be conditional
    auto *br = cast<BranchInst>(BB.getTerminator());
    if (br->isUnconditional())
      continue;

    // Only for predicates that are defined in a different basic block
    auto *pred = cast<Instruction>(br->getCondition());
    if (pred->getParent() == &BB)
      continue;

    PHINode *phi = PHINode::Create(pred->getType(), 1, pred->getName(), BB.getFirstNonPHI());
    phi->addIncoming(pred, &BB);

    br->setCondition(phi);
  }
}

void delete_dead_instructions(Function *F, std::set<Instruction*> &dependences){

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
}

/*
  Do a BFS and find basic blocks from *curr to *postdom
*/
std::vector<PathEdge*> bfs(std::set<BasicBlock*> &alive, BasicBlock *curr, BasicBlock *postdom){
  std::queue<BasicBlock*> q;
  std::set<BasicBlock*> visited;
  std::vector<PathEdge*> path;

  q.push(curr);

  while (!q.empty()){
    auto *node = q.front();
    q.pop();

    if (node == postdom)
      break;

    for (auto *succ : successors(node)){
      if (visited.find(succ) != visited.end()
          || alive.find(succ) != alive.end())
        continue;

      visited.insert(succ);

      path.push_back(new PathEdge(node, succ));
      q.push(succ);
    }
  }

  return path;
}

/*
  If @curr can be removed, than his entire region of influence can be removed as well.
  Thus, we remove curr by making each predecessor point to @postdom.
  From theory, the post-dominator of a basic block is unique.
*/
void remove_block(PostDominatorTree &PDT,
                  std::set<BasicBlock *> &alive,
                  std::vector<BasicBlock *> &dead,
                  BasicBlock *curr,
                  BasicBlock *postdom) {
  if (num_pred(curr) > 0){
    for (BasicBlock *pred : get_predecessors(curr)){
      connect_pred_to_postdom(pred, curr, postdom);
    }

    /* We do a bfs to find the set of reachable blocks starting from @curr. Those blocks
      can be safely deleted.
      
      For each block we delete, we must also update its reference on the appropriated
      postdominator structure.
    */
    auto path = bfs(alive, curr, postdom);
    for (auto *p : path){
      replace_jump_by_selfedge(p->from);
      // PDT.deleteEdge(p->from, p->to);
      errs() << "Updating PDT: " << p->from->getName() << " -> " << p->to->getName() << "\n";
    }

    // for (auto *p : path){
    //   if (p->from)
    //     erase_block(PDT, p->from);
    // }
  } else {
    // to-do
  }

  // curr->getParent()->viewCFG();
}

void ProgramSlicing::slice(Function *F, Instruction *I) {
  errs() << "[INFO]: Applying slicing on: " << F->getName() << "\n";

  // LoopInfo LI(DT);
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

  split_predicate_live(F);

  ProgramDependenceGraph PDG;
  PDG.compute_dependences(F);
  std::set<Instruction*> dependences = PDG.get_dependences_for(I);
  // PDG.get_dependence_graph()->to_dot();

  delete_dead_instructions(F, dependences);
  delete_empty_blocks(F);

  std::set<BasicBlock*> alive_blocks = compute_alive_blocks(F, dependences);
  std::vector<BasicBlock*> dead_blocks = compute_dead_blocks(F, alive_blocks);

  PostDominatorTree PDT;
  // DominatorTree DT(*F);
  // DominanceFrontier DF;
  // DF.analyze(DT);

  // RegionInfo RI;
  // RI.recalculate(*F, &DT, &PDT, &DF);

  while (!dead_blocks.empty()){
    PDT.recalculate(*F);
    auto *curr = *dead_blocks.begin();
    dead_blocks.erase(dead_blocks.begin());
    BasicBlock *postdom = PDT.getNode(curr)->getIDom()->getBlock();
    errs() << "curr: " << curr->getName() << " -> postdom: " << postdom->getName() << "\n";
    remove_block(PDT, alive_blocks, dead_blocks, curr, postdom);
  }

  // u.run(*F, FAM);
  // si.run(*F, FAM);
  sf.run(*F, FAM);

  // delete_blocks(F, alive_blocks);

  // F->viewCFG();
  // sf.run(*F, FAM);

  // VerifierPass ver;
  // ver.run(*F, FAM);

}

}  // namespace phoenix
