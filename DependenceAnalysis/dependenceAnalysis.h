#pragma once

#include "dependenceGraph.h"

using namespace llvm;

namespace phoenix {

class DependenceAnalysis {
private:
  Value *get_predicate(BasicBlock *X, BasicBlock *Y, Value *old_pred);
  void update_control_dependences(BasicBlock *Y, Value *pred);

  void compute_dependences(DomTreeNodeBase<BasicBlock> *X, Value *pred);
  void compute_dependences();

  void print_graph();

public:
  std::set<const Value *> get_dependences_transition(const Value *start);
  DependenceAnalysis(DominatorTree *DT, PostDominatorTree *PDT);

private:
  DominatorTree *DT;
  PostDominatorTree *PDT;

  DependenceGraph *DG;
};

} // end namespace phoenix