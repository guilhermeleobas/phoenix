#include "dependenceGraph.h"

using namespace llvm;

namespace phoenix {

void DependenceGraph::add_edge(const DependenceEdge *edge) {
  auto &mapa = *this;
  mapa[edge->u].push_back(edge);
}

}; // namespace phoenix
