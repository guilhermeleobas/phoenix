#pragma once

#include "node.h"

struct NodeCompare {
  bool operator() (const phoenix::Node *a, const phoenix::Node *b) const {
    return a->distance() < b->distance();
  }
};

class NodeSet : public std::set<phoenix::Node*, NodeCompare> {};