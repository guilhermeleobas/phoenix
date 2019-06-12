#pragma once

#include "NodeSet.h"

// We store in this struct a pointer to the store and a set of (reachable) nodes
// that when 0, cause the store to be silent
struct ReachableNodes {
  StoreInst *store;
  LoadInst *load;
  Instruction *arithInst;
  NodeSet nodes;

  ReachableNodes() = delete;
  ReachableNodes(StoreInst *store, LoadInst *load, Instruction *arithInst, NodeSet nodes) :
    store(store), load(load), arithInst(arithInst), nodes(nodes) {}

  StoreInst *get_store() const { return store; }
  LoadInst *get_load() const { return load; }
  Instruction *get_arith_inst() const { return arithInst; }
  NodeSet get_nodeset() const { return nodes; }
};