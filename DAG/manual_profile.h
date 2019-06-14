#pragma once

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

#include "../Identify/Geps.h"
#include "../ProgramSlicing/ProgramSlicing.h"
#include "NodeSet.h"
#include "insertIf.h"

using namespace llvm;

namespace phoenix {

static void manual_profile(Function *F,
                           LoopInfo *LI,
                           DominatorTree *DT,
                           PostDominatorTree *PDT,
                           ProgramSlicing *PS,
                           std::vector<ReachableNodes> &stores_in_loop,
                           unsigned num_stores);

void manual_profile(Function *F,
                    LoopInfo *LI,
                    DominatorTree *DT,
                    PostDominatorTree *PDT,
                    ProgramSlicing *PS,
                    std::vector<ReachableNodes> &reachables);

};  // namespace phoenix