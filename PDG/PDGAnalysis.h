#pragma once

#include "dependenceGraph.h"

using namespace llvm;

namespace phoenix {

class PDG {
private:
  Value *get_predicate(BasicBlock *X, BasicBlock *Y, Value *old_pred);
  void create_control_edges(BasicBlock *Y, Value *pred);
  void compute_control_dependences(DomTreeNodeBase<BasicBlock> *X, Value *pred);

  void create_data_edges(Value *start);
  void compute_data_dependences(Function *F);

  void print_graph();

public:
  std::set<Value *> get_dependences_transition(Value *start);
  PDG(Function *F, DominatorTree *DT, PostDominatorTree *PDT);

private:
  DominatorTree *DT;
  PostDominatorTree *PDT;

  DependenceGraph *DG;
};

} // end namespace phoenix