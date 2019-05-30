#pragma once

#include "llvm/IR/Instructions.h" // To have access to the Instructions.

using namespace llvm;

namespace phoenix {

enum DependenceType {
  DT_Data,
  DT_Control,
};

struct DependenceEdge {
  const Value *u, *v;
  DependenceType dt;

  DependenceEdge(const Value *u, const Value *v, DependenceType dt)
      : u(u), v(v), dt(dt) {}
};

class DependenceGraph
    : public std::map<const Value *, std::vector<const DependenceEdge *>> {
public:
  void add_edge(const DependenceEdge *edge);
};

} // end namespace phoenix