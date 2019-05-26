#pragma once

using namespace llvm;

namespace phoenix {

class Dependency {

private:
  struct Edge {
    BasicBlock *head, *tail; 
    Edge(BasicBlock *head, BasicBlock *tail): head(head), tail(tail){}
  };

  Function *F;
  PostDominatorTree *PDT;

  /// \brief Get all edges whose @head does not post dominates @tail
  std::vector<Edge> getNonPostDomEdges();
  void updateControlDependencies(const std::vector<Dependency::Edge> &v);

public:

  Dependency(Function *F, PostDominatorTree *PDT);

  /// \brief 
  // std::vectailr<Instruction *> get_data_dependency();

  /// \brief 
  std::vector<BasicBlock *> get_control_dependency();
};

} // end namespace phoenix