#include "ProgramSlicing.h"

#include "../PDG/PDGAnalysis.h"
#include "llvm/Analysis/DominanceFrontier.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/RegionInfo.h"
#include "llvm/IR/Verifier.h"

#include "llvm/CodeGen/UnreachableBlockElim.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Transforms/Scalar/DCE.h"
#include "llvm/Transforms/Scalar/SimplifyCFG.h"
#include "llvm/Transforms/Scalar/Sink.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/LoopUtils.h"

#include "llvm/Support/GraphWriter.h"

#include <iterator>
#include <queue>
#include <set>

namespace phoenix {

void ProgramSlicing::set_exit_block(Function *F) {
  BasicBlock *F_exit = nullptr;

  for (BasicBlock &BB : *F) {
    if (isa<ReturnInst>(BB.getTerminator()))
      F_exit = &BB;
  }

  assert(F_exit != nullptr && "F_exit is nullptr");
  BasicBlock *exit = BasicBlock::Create(F->getContext(), "function_exit", F, F_exit);

  IRBuilder<> Builder(exit);
  Builder.CreateRetVoid();

  Instruction *ret = F_exit->getTerminator();

  Builder.SetInsertPoint(ret);
  Builder.CreateBr(exit);

  ret->dropAllReferences();
  ret->eraseFromParent();
}

std::set<BasicBlock *> compute_alive_blocks(Function *F, std::set<Instruction *> &dependences) {
  std::set<BasicBlock *> alive;

  for (auto &BB : *F) {
    for (Instruction &I : BB) {
      if (dependences.find(&I) != dependences.end()) {
        // errs() << "alive: " << BB.getName() << "\n";
        alive.insert(&BB);
        break;
      }
    }
  }

  return alive;
}

unsigned num_pred(BasicBlock *BB) {
  unsigned i = 0;
  for (auto it = pred_begin(BB); it != pred_end(BB); ++it)
    ++i;
  return i;
}

unsigned num_succ(BasicBlock *BB) {
  unsigned i = 0;
  for (auto it = succ_begin(BB); it != succ_end(BB); ++it)
    ++i;
  return i;
}

bool replace_jump_by_selfedge(BasicBlock *curr) {
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
void replace_direct_jump(BasicBlock *pred, BasicBlock *postdom) {
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
void replace_conditional_jump(BasicBlock *pred, BasicBlock *curr, BasicBlock *postdom) {
  BranchInst *term = cast<BranchInst>(pred->getTerminator());
  for (unsigned i = 0; i < term->getNumSuccessors(); i++) {
    if (term->getSuccessor(i) == curr) {
      // errs() << "Setting successor of : " << pred->getName() << " -> " << succ->getName() <<
      // "\n";
      term->setSuccessor(i, postdom);
      return;
    }
  }
}

/*
  As the name suggests, this method transforms a constant into a value
  by storing the constant as a global variable and then loading from it.
*/
Value* constant_to_value(Function *F, BasicBlock *BB, ConstantData *c){
  // errs() << "Basic Block: " << *BB << "\n";
  Module *M = F->getParent();
  std::string name = std::string(BB->getName()) + "_gv_" + std::to_string(random() % 1000000);
  M->getOrInsertGlobal(name, c->getType());
  auto *gv = M->getNamedGlobal(name);
  gv->setInitializer(c);

  IRBuilder<> Builder(BB->getFirstNonPHI());
  Value *v = Builder.CreateLoad(gv);
  return v;
}

/*
  Given a PHI of the form

  for.cond.s:
    %i.0.s = phi i32 [ 10, %if.then24.s ], [ %inc40.s, %for.body.s ]
    ...                ^^^^^^^^^^^^^^^

  Replace it by

  if.then24.s:
    ...
    %v = load i32 %some_global_ptr

  for.cond.s:
    %i.0.s = phi i32 [ %v, %if.then24.s ], [ %inc40.s, %for.body.s ]
    ...                ^^^^^^^^^^^^^^^

  This is important because memory isn't in SSA form, so this is a way
  to capture/create a data dependency.
*/
void replace_noninst_phientry_toinst(Function *F){
  for (BasicBlock &BB : *F){
    for (PHINode &PHI : BB.phis()){
      for (unsigned Op = 0, NumOps = PHI.getNumOperands(); Op != NumOps; ++Op){
        if (ConstantData *c = dyn_cast<ConstantData>(PHI.getIncomingValue(Op))){
          BasicBlock *BB = PHI.getIncomingBlock(Op);
          Value *v = constant_to_value(F, BB, c);
          PHI.setIncomingValue(Op, v);
        }
      }
    }
  }
}

/*
  Iterate over PHI nodes and check if @curr is being used as an input;
  If it is, remove it
*/
void remove_from_phi(BasicBlock *curr){
  Function *F = curr->getParent();

  for (BasicBlock &BB : *F){
    for (PHINode &PHI : BB.phis()){
      for (unsigned Op = 0, NumOps = PHI.getNumOperands(); Op != NumOps; ++Op){
        if (PHI.getIncomingBlock(Op) == curr){
          PHI.removeIncomingValue(Op);
        }
      }
    }
  }
}

// bool fix_phi_nodes(BasicBlock *curr) {

//   auto *Old = curr;
//   auto *New = prev;
//   for (PHINode &phi : succ->phis()){
//     for (unsigned Op = 0, NumOps = phi.getNumOperands(); Op != NumOps; ++Op)
//       if (phi.getIncomingBlock(Op) == Old)
//         phi.setIncomingBlock(Op, New);
//   }

// }

BasicBlock *get_alive_parent_dom(DominatorTree *DT, std::set<BasicBlock*> &alive_blocks, BasicBlock *BB){

  BasicBlock *curr = BB;
  while(curr != nullptr && alive_blocks.find(curr) == alive_blocks.end()){
    curr = DT->getNode(curr)->getIDom()->getBlock();
  }

  if (curr != nullptr)
    return curr;

  llvm_unreachable("Could not find an alive dominator");
}

/*
  For each PHI node, if the incoming block is dead, then, replace the block
  by the actual block where the variable is defined.
*/
void fix_phi_nodes(Function *F, DominatorTree *DT, std::set<BasicBlock*> &alive_blocks){
  for (BasicBlock &BB : *F){
    for (PHINode &PHI : BB.phis()){
      for (unsigned Op = 0, NumOps = PHI.getNumOperands(); Op != NumOps; ++Op){

        auto *block = PHI.getIncomingBlock(Op);
        // check if @block is dead or alive
        if (alive_blocks.find(block) == alive_blocks.end()){
          // @block is dead
          PHI.setIncomingBlock(Op, get_alive_parent_dom(DT, alive_blocks, block));
        }

      }
    }
  }
}


/*
  Remove the edge from @pred to @curr and create a new one from @pred to @postdom
*/
void connect_pred_to_postdom(BasicBlock *pred, BasicBlock *curr, BasicBlock *postdom) {
  // errs() << "Connecting: " << pred->getName() << " to " << postdom->getName() << "\n";

  if (num_succ(pred) == 1)
    replace_direct_jump(pred, postdom);
  else
    replace_conditional_jump(pred, curr, postdom);
}

bool fix_conditional_jump_to_same_block(BasicBlock *BB) {
  /*
    If BB contains a instruction of the form:
     br T %val, label %succ, label %succ
    We replace it by a direct jump:
     br label %succ
  */

  if (BB->empty() || BB->getTerminator() == nullptr)
    return false;

  if (!isa<BranchInst>(BB->getTerminator()))
    return false;

  BranchInst *term = cast<BranchInst>(BB->getTerminator());

  if (term->isUnconditional())
    return false;

  if (term->getSuccessor(0) == term->getSuccessor(1)) {
    BranchInst *rep = BranchInst::Create(term->getSuccessor(0), term);
    term->dropAllReferences();
    term->eraseFromParent();
  }

  return true;
}

void delete_branch(BasicBlock *BB) {
  assert(isa<BranchInst>(BB->getTerminator()));
  BranchInst *term = cast<BranchInst>(BB->getTerminator());
  term->dropAllReferences();
  term->eraseFromParent();
}

bool remove_successor(BasicBlock *BB, BasicBlock *succ) {
  if (!isa<BranchInst>(BB->getTerminator()))
    return false;

  BranchInst *term = cast<BranchInst>(BB->getTerminator());

  if (term->isUnconditional()) {
    delete_branch(BB);
  } else {
    unsigned idx = (term->getSuccessor(0) == succ) ? 1 : 0;
    BasicBlock *other = term->getSuccessor(idx);
    term->setSuccessor((idx + 1) % 2, other);
    fix_conditional_jump_to_same_block(BB);
  }

  return true;
}

std::vector<BasicBlock *> get_successors(BasicBlock *BB) {
  std::vector<BasicBlock *> v;
  for (BasicBlock *succ : successors(BB)) {
    v.push_back(succ);
  }
  return v;
}

std::vector<BasicBlock *> get_predecessors(BasicBlock *BB) {
  std::vector<BasicBlock *> v;
  for (BasicBlock *pred : predecessors(BB)) {
    v.push_back(pred);
  }
  return v;
}

void erase_block(BasicBlock *BB) {
  // errs() << "Deleting block: " << BB->getName() << "\n\n";
  // remove_from_phi(BB);
  // fix_phi_nodes(BB);
  BB->dropAllReferences();
  BB->eraseFromParent();
}

bool merge_return_blocks(Function *F) {
  std::vector<BasicBlock *> v;

  for (BasicBlock &BB : *F) {
    if (!BB.empty() && isa<ReturnInst>(BB.getTerminator()))
      v.push_back(&BB);
  }

  if (v.size() == 1)
    return false;

  BasicBlock *ret = BasicBlock::Create(F->getContext(), "function_exit", F, nullptr);
  auto *ri = ReturnInst::Create(F->getContext(), nullptr, ret);

  for (BasicBlock *BB : v) {
    auto *term = BB->getTerminator();
    BranchInst *br = BranchInst::Create(ret, term);
    term->dropAllReferences();
    term->eraseFromParent();
  }

  return true;
}

std::vector<BasicBlock *> compute_dead_blocks(Function *F, std::set<BasicBlock *> &alive_blocks) {
  df_iterator_default_set<BasicBlock *> Reachable;
  std::vector<BasicBlock *> d;

  for (BasicBlock *BB : depth_first_ext(F, Reachable)) {
    if (!BB->empty() && isa<ReturnInst>(BB->getTerminator()))
      continue;
    if (alive_blocks.find(BB) == alive_blocks.end()) {
      d.push_back(BB);
    }
  }

  std::reverse(d.begin(), d.end());

  return d;
}

void delete_empty_blocks(Function *F) {
  std::queue<BasicBlock *> q;
  for (auto &BB : *F) {
    if (BB.empty()) {
      for (BasicBlock *pred : get_predecessors(&BB))
        remove_successor(pred, &BB);
      q.push(&BB);
    }
  }

  while (!q.empty()) {
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
void split_predicate_live(Function *F) {
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
    phi->addIncoming(pred, pred->getParent());

    br->setCondition(phi);
  }
}

void delete_dead_instructions(Function *F, std::set<Instruction *> &dependences) {
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
std::vector<PathEdge *> bfs(std::set<BasicBlock *> &alive, BasicBlock *curr, BasicBlock *postdom) {
  std::queue<BasicBlock *> q;
  std::set<BasicBlock *> visited;
  std::vector<PathEdge *> path;

  q.push(curr);

  while (!q.empty()) {
    auto *node = q.front();
    q.pop();

    if (node == postdom)
      break;

    for (auto *succ : successors(node)) {
      if (visited.find(succ) != visited.end() || 
        (succ != postdom && alive.find(succ) != alive.end()))
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
  if (num_pred(curr) > 0) {
    for (BasicBlock *pred : get_predecessors(curr)) {
      connect_pred_to_postdom(pred, curr, postdom);
    }
  } 

  /* We do a bfs to find the set of reachable blocks starting from @curr. Those blocks
    can be safely deleted.

    For each block we delete, we must also update its reference on the appropriated
    postdominator structure.
  */
  auto path = bfs(alive, curr, postdom);
  for (auto *p : path) {
    replace_jump_by_selfedge(p->from);
    // errs() << "Updating PDT: " << p->from->getName() << " -> " << p->to->getName() << "\n";
  }

  for (auto *p : path)
    if (p->from)
      erase_block(p->from);

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
  replace_noninst_phientry_toinst(F);

  ProgramDependenceGraph PDG;
  PDG.compute_dependences(F);
  std::set<Instruction *> dependences = PDG.get_dependences_for(I);
  // PDG.get_dependence_graph()->to_dot();

  delete_dead_instructions(F, dependences);
  delete_empty_blocks(F);

  std::set<BasicBlock *> alive_blocks = compute_alive_blocks(F, dependences);
  std::vector<BasicBlock *> dead_blocks = compute_dead_blocks(F, alive_blocks);

  // F->viewCFG();

  PostDominatorTree PDT;
  DominatorTree DT(*F);

  fix_phi_nodes(F, &DT, alive_blocks);
  // DominanceFrontier DF;
  // DF.analyze(DT);

  // RegionInfo RI;
  // RI.recalculate(*F, &DT, &PDT, &DF);

  while (!dead_blocks.empty()) {
    PDT.recalculate(*F);
    auto *curr = *dead_blocks.begin();
    dead_blocks.erase(dead_blocks.begin());
    BasicBlock *postdom = PDT.getNode(curr)->getIDom()->getBlock();
    // errs() << "curr: " << curr->getName() << " -> postdom: " << postdom->getName() << "\n";
    remove_block(PDT, alive_blocks, dead_blocks, curr, postdom);
  }


  // u.run(*F, FAM);
  // si.run(*F, FAM);
  // F->viewCFG();
  sf.run(*F, FAM);

  // delete_blocks(F, alive_blocks);

  // VerifierPass ver;
  // ver.run(*F, FAM);
}

}  // namespace phoenix
