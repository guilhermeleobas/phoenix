#pragma once

#include "llvm/ADT/Statistic.h"  // For the STATISTIC macro.
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/IR/Constants.h"  // For ConstantData, for instance.
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"  // To use the iterator instructions(f)
#include "llvm/IR/Instructions.h"  // To have access to the Instructions.
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Metadata.h"
#include "llvm/Support/raw_ostream.h"  // For dbgs()
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/IR/DebugInfoMetadata.h" // For DILocation

using namespace llvm;

#include "visitor.h"

class Visitor;

namespace phoenix {

#define MAKE_VISITABLE \
  virtual void accept(Visitor &vis) override { \
    vis.visit(this); \
  } \

std::string getFileName(Instruction *I);
int getLineNo(Value *V);

class Counter{
 public:
  static int ID;
  const int uniq;
 public:
  Counter(): uniq(ID++){}
  const int getID() const { return uniq; }
};

class Label: private Counter{
 public:
  std::string getLabel() const {
    return "node" + std::to_string(getID());
  }
};

class Node: public Label {
 private:
  Value *V;


 public:
  Node(Value *V): V(V), Label() {}
  virtual ~Node() {}

  Value* getValue() const { return V; }
  Instruction* getInst() const { return dyn_cast<Instruction>(V); }

  std::string name(void) const {
    return std::string(getValue()->getName());
  }

  virtual std::string instType(void) const {
    if (Instruction *I = dyn_cast<Instruction>(V)){
      return std::string(I->getOpcodeName());
    }
    else {
      std::string type_str;
      llvm::raw_string_ostream rso(type_str);
      getValue()->getType()->print(rso);
      return type_str;
    }
  }

  virtual void accept(Visitor &v) = 0;
};



class UnaryNode : public Node {
 public:
  Node *child;
  UnaryNode(Node *child, Instruction *I): child(child), Node(I){}

  MAKE_VISITABLE;
};


class BinaryNode : public Node {
 public:
  Node *left;
  Node *right;

  BinaryNode(Node *left, Node *right, Instruction *I): left(left), right(right), Node(I){}
  MAKE_VISITABLE;

};

class TerminalNode : public Node {
 public:
  TerminalNode(Value *V) : Node(V) {}
  MAKE_VISITABLE;

};

class ForeignNode : public TerminalNode {
 public:
  ForeignNode(Value *V) : TerminalNode(V){}
};

class ConstantNode : public TerminalNode {
 public:
  ConstantNode(Constant *C) : TerminalNode(C){}
};

class ConstantIntNode : public ConstantNode {
 public:
  ConstantIntNode(Constant *C) : ConstantNode(C){}
};

}  // namespace phoenix
