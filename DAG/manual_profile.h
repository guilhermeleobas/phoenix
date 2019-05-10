#pragma once

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
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h" // For dbgs()
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

using namespace llvm;

#define DUMP(F, node) \
  errs() << "[" << F->getName() << "] " << *node << "\n"

#define SWAP_SUCCESSOR(BB, VMAP)                       \
do {                                                   \
  auto *br = BB->getTerminator();                      \
  for (unsigned i=0; i<br->getNumSuccessors(); ++i){   \
    auto *suc = br->getSuccessor(i);                   \
    if (VMap[suc])                                     \
    br->setSuccessor(i, cast<BasicBlock>(VMap[suc]));  \
  }                                                    \
} while (0)                                            \

namespace phoenix{

static Loop *get_outer_loop(LoopInfo *LI, BasicBlock *BB) {
  Loop *L = LI->getLoopFor(BB);

  BasicBlock *header = L->getHeader();
  unsigned depth = L->getLoopDepth();
  if (depth <= 1)
    return L;

  for (BasicBlock *pred : predecessors(header)) {
    Loop *L2 = LI->getLoopFor(pred);

    if (L2 == nullptr)
      continue;

    if (L2->getLoopDepth() < depth) {
      return get_outer_loop(LI, pred);
    }
  }

  llvm_unreachable("unreachable state");
}

static BasicBlock *split_pre_header(Loop *L, LoopInfo *LI, DominatorTree *DT) {
  auto *from = L->getLoopPreheader();
  auto *to = L->getHeader();
  // new versions of LLVM requires a fifth parameter (MemorySSA)
  return SplitEdge(from, to, DT, LI);
}

// Given the original loop and the cloned loop
// This function creates a call a function that performs a sampling
// and insert code to decide which loop is executed!
static void fill_control(BasicBlock *control) {

}

/// \brief Clones the original loop \p OrigLoop structure
/// and keeps it ready to add the basic blocks.
static void createNewLoops(Loop *OrigLoop, LoopInfo *LI, Loop *ParentLoop,
   std::map<Loop*, Loop*>  &ClonedLoopMap) {
  if (OrigLoop->empty()) return;

  for (auto CurrLoop :  OrigLoop->getSubLoops()) {
    Loop *NewLoop = LI->AllocateLoop();
    ParentLoop->addChildLoop(NewLoop);
    ClonedLoopMap[CurrLoop] = NewLoop;

    // Recursively add the new loops.
    createNewLoops(CurrLoop, LI, NewLoop, ClonedLoopMap);
  }
}

static void print(Loop *L){
  errs() << "pre header: " << *L->getLoopPreheader() << "\n";
  for (auto *BB : L->getBlocks()){
    errs() << *BB << "\n";
  }
  errs() << "\n";
}

static void fixLoopBranches(Loop *ClonedLoop, BasicBlock *pre, ValueToValueMapTy &VMap){
  // errs() << "cloned: " << *ClonedLoop << "\n";

  SWAP_SUCCESSOR(pre, VMap);

  for (auto *BB : ClonedLoop->blocks()){
    auto *br = BB->getTerminator();
    for (unsigned i=0; i<br->getNumSuccessors(); ++i){
      auto *suc = br->getSuccessor(i);
      if (VMap[suc])
        br->setSuccessor(i, cast<BasicBlock>(VMap[suc]));
    }
  }

  for (auto L : ClonedLoop->getSubLoops()){
    fixLoopBranches(L, L->getLoopPreheader(), VMap);
  }
}

/// \brief Clones a loop \p OrigLoop.  Returns the loop and the blocks in \p
/// Blocks.
///
/// Updates LoopInfo and DominatorTree assuming the loop is dominated by block
/// \p LoopDomBB.  Insert the new blocks before block specified in \p Before.
static Loop *cloneLoopWithPreheader(BasicBlock *Before, BasicBlock *LoopDomBB,
                                   Loop *OrigLoop, ValueToValueMapTy &VMap,
                                   const Twine &NameSuffix, LoopInfo *LI,
                                   DominatorTree *DT,
                                   SmallVectorImpl<BasicBlock *> &Blocks) {


  Function *F = OrigLoop->getHeader()->getParent();
  Loop *ParentLoop = OrigLoop->getParentLoop();

  Loop *NewLoop = LI->AllocateLoop(); 
  if (ParentLoop)
    ParentLoop->addChildLoop(NewLoop);
  else
    LI->addTopLevelLoop(NewLoop);

  // Map each old Loop with new one.
  std::map<Loop*, Loop*> ClonedLoopMap;
  // Add the top level loop provided for cloning.
  ClonedLoopMap[OrigLoop] = NewLoop;

  // Recursively clone the loop structure.
  createNewLoops(OrigLoop, LI, NewLoop, ClonedLoopMap);

  BasicBlock *OrigPH = OrigLoop->getLoopPreheader();
  assert(OrigPH && "No preheader");
  BasicBlock *NewPH = deep_clone(OrigPH, VMap, NameSuffix, F);
  // To rename the loop PHIs.
  VMap[OrigPH] = NewPH;
  Blocks.push_back(NewPH);

  // Update LoopInfo.
  if (ParentLoop)
    ParentLoop->addBasicBlockToLoop(NewPH, *LI);

  // Update DominatorTree.
  DT->addNewBlock(NewPH, LoopDomBB);

  for (BasicBlock *BB : OrigLoop->getBlocks()) {
    BasicBlock *NewBB = CloneBasicBlock(BB, VMap, NameSuffix, F);
    VMap[BB] = NewBB;

    // Get the innermost loop for the BB.
    Loop* L = LI->getLoopFor(BB);
    // Get the corresponding cloned loop.
    Loop* NewClonedLoop = ClonedLoopMap[L];
    assert(NewClonedLoop && "Could not find the corresponding cloned loop");
    // Update LoopInfo.
    NewClonedLoop->addBasicBlockToLoop(NewBB, *LI);

    // Add DominatorTree node. After seeing all blocks, update to correct IDom.
    DT->addNewBlock(NewBB, NewPH);

    Blocks.push_back(NewBB);
  }

  for (BasicBlock *BB : OrigLoop->getBlocks()) {
    // Update DominatorTree.
    BasicBlock *IDomBB = DT->getNode(BB)->getIDom()->getBlock();
    DT->changeImmediateDominator(cast<BasicBlock>(VMap[BB]),
                                 cast<BasicBlock>(VMap[IDomBB]));
  }

  for (BasicBlock *BB : NewLoop->blocks()){
    remap_nodes(BB, VMap);
  }

  // Move them physically from the end of the block list.
  F->getBasicBlockList().splice(Before->getIterator(), F->getBasicBlockList(),
                                NewPH);
  F->getBasicBlockList().splice(Before->getIterator(), F->getBasicBlockList(),
                                NewLoop->getHeader()->getIterator(), F->end());

  fixLoopBranches(NewLoop, NewPH, VMap);

  return NewLoop;
}

void manual_profile(Function *F, LoopInfo *LI, DominatorTree *DT, const Geps &g,
                    NodeSet &s) {
  // Store the loops already processed
  SmallSet<Loop *, 5> loops;

  for (phoenix::Node *node : s) {
    DUMP(F, node);
    Instruction *I = node->getInst();
    Loop *outer = get_outer_loop(LI, I->getParent());

    // already optimized
    if (loops.find(outer) != loops.end())
      continue;
    loops.insert(outer);

  }

  errs() << "\n";

  for (Loop *L : loops) {
    // p --> m --> h
    auto *p = L->getLoopPreheader();
    auto *h = L->getHeader(); 
    // auto *m = split_pre_header(L, LI, DT);
    ValueToValueMapTy VMap;
    SmallVector<BasicBlock *, 32> Blocks;
    Loop *c = phoenix::cloneLoopWithPreheader(p, p, L, VMap, ".c", LI, DT, Blocks);
    print(L);
    print(c);
  }

  // for (Argument &arg : F->args()){
  //   errs() << arg << "\n";
  // }

  // for (const auto &node : s){
  //   errs() << "[" << F->getName() << "]:" << *node << "\n";
  // }
}

}; // end namespace phoenix;