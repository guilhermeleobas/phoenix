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

#include <queue>
#include <stack>
#include <vector>

#include "dependenceAnalysis.h"
#include "dependenceGraph.h"

using namespace llvm;

#define CONTAINS(v, value) (std::find(v.begin(), v.end(), value) != v.end())

namespace phoenix {

/// \brief We update the predicate iff
///   1. X ends with a conditional jump
///   2. Y does not post-dominates X
///
Value *DependenceAnalysis::get_predicate(BasicBlock *X, BasicBlock *Y,
                                         Value *old_pred) {
  auto *ti = X->getTerminator();

  if (!isa<BranchInst>(ti))
    return old_pred;

  BranchInst *br = cast<BranchInst>(ti);

  if (br->isUnconditional() || PDT->properlyDominates(Y, X))
    return old_pred;

  return br->getCondition();
}

// Create control dependence edges from each y \in Y to the predicate *pred
void DependenceAnalysis::update_control_dependences(BasicBlock *Y,
                                                    Value *pred) {
  if (pred == nullptr)
    return;

  for (Value &y : *Y) {
    DG->add_edge(new DependenceEdge(&y, pred, DT_Control));
  }
}

void DependenceAnalysis::compute_dependences(DomTreeNodeBase<BasicBlock> *X,
                                             Value *pred) {

  update_control_dependences(X->getBlock(), pred);

  for (auto *Y : *X) {
    Value *new_pred = get_predicate(X->getBlock(), Y->getBlock(), pred);
    compute_dependences(Y, new_pred);
  }
  return;
}

void DependenceAnalysis::compute_dependences() {
  auto *root = this->DT->getRootNode();
  compute_dependences(root, nullptr);
}

void DependenceAnalysis::print_graph() {
  for (auto &u : *DG) {
    //
    get_dependences_transition(u.first);
  }
}

std::set<const Value *>
DependenceAnalysis::get_dependences_transition(const Value *start) {
  //
  std::set<const Value *> s;
  std::queue<const Value *> q;

  s.insert(start);
  q.push(start);

  while (true) {
    const Value *u = q.front();
    q.pop();

    if (DG->find(u) == DG->end())
      break;

    errs() << *u << "\n";

    for (const DependenceEdge *edge : DG->operator[](u)) {
      if (s.find(edge->v) != s.end())
        continue;

      s.insert(edge->v);
      q.push(edge->v);
    }
  }

  for (const Value *v : s)
    errs() << *v << "\n";
  errs() << "\n\n-----\n\n\n";

  return s;
}

DependenceAnalysis::DependenceAnalysis(DominatorTree *DT,
                                       PostDominatorTree *PDT)
    : DT(DT), PDT(PDT) {
  DG = new DependenceGraph();
  compute_dependences();
  print_graph();
}

} // namespace phoenix