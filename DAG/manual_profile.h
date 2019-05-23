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
#include "llvm/Transforms/Utils/Cloning.h"

#include "../Identify/Geps.h"
#include "NodeSet.h"
#include "insertIf.h"

using namespace llvm;

namespace phoenix{


struct LoopOptProperties {
  BasicBlock *pp; // the pre preHeader
  Loop *L;
  Loop *C;
  ValueToValueMapTy VMap;
};

using LoopOptPropertiesMap = std::map<Loop*, LoopOptProperties*>;

static Loop *get_outer_loop(LoopInfo *LI, BasicBlock *BB);

static BasicBlock *split_pre_header(Loop *L, LoopInfo *LI, DominatorTree *DT);

// Creates a call to the sampling function
//  - @F : The function
//  - @pp : The loop pre preheader 
//  - @L/@C : Original/Cloned loops
static void fill_control(Function *F, BasicBlock *pp, Loop *L, Loop *C, LoopInfo *LI, DominatorTree *DT);

/// \brief Clones the original loop \p OrigLoop structure
/// and keeps it ready to add the basic blocks.
static void createNewLoops(Loop *OrigLoop, LoopInfo *LI, Loop *ParentLoop,
   std::map<Loop*, Loop*>  &ClonedLoopMap);

static void print(Loop *L);

/// \brief Iterates over all basic blocks in the cloned loop and fixes all the jump instructions
static void fixLoopBranches(Loop *ClonedLoop, BasicBlock *pre, ValueToValueMapTy &VMap);

/// \brief Clones the loop for profilling
///  - Each instruction
static void createProfileLoop(BasicBlock *pp, LoopInfo *LI, DominatorTree *DT);

/// \brief Clones a loop \p OrigLoop.  Returns the loop and the blocks in \p
/// Blocks.
///
/// Updates LoopInfo and DominatorTree assuming the loop is dominated by block
/// \p LoopDomBB.  Insert the new blocks before block specified in \p Before.
static Loop *cloneLoopWithPreheader(BasicBlock *Before, BasicBlock *LoopDomBB,
                                   Loop *OrigLoop, ValueToValueMapTy &VMap,
                                   const Twine &NameSuffix, LoopInfo *LI,
                                   DominatorTree *DT,
                                   SmallVectorImpl<BasicBlock *> &Blocks);

void manual_profile(Function *F, LoopInfo *LI, DominatorTree *DT, const Geps &g,
                    NodeSet &s);

}; // namespace phoenix