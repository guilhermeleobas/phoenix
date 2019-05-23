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
#include "constantWrapper.h"
#include "../Identify/Position.h"

class Visitor;

namespace phoenix {

#define MAKE_CLASSOF(nk_begin, nk_end)    \
  static bool classof(const Node *node) { \
    return node->getKind() >= nk_begin && \
           node->getKind() <= nk_end;     \
  }                                       \

#define MAKE_VISITABLE \
  virtual void accept(Visitor &vis) override { \
    vis.visit(this); \
  }

std::string getFileName(Instruction *I);
int getLineNo(Value *V);

class Counter{
 private:
  static int ID;
  const int uniq;
 public:
  Counter(): uniq(ID++){}
  const int getID() const { return uniq; }
};

// class Label: private Counter{
//  public:
//   std::string getLabel() const {
//     return "node" + std::to_string(getID());
//   }
// };


class Node: public Counter, public ConstantWrapper {
 public:
  enum NodeKind {
    NK_UnaryNode,
      NK_StoreNode,
      NK_CastNode,
    NK_UnaryNode_End,
    
    NK_BinaryNode,
      NK_TargetOpNode,
    NK_BinaryNode_End,

    NK_TerminalNode,
      NK_LoadNode,
      NK_ForeignNode,
      NK_ArgumentNode,
      NK_ConstantNode,
      NK_ConstantIntNode,
    NK_TerminalNode_End,
  };
 private:
  Value *V;

  const NodeKind Kind;

 public:
  Node(Value *V, NodeKind Kind): V(V), Kind(Kind), Counter() {}
  virtual ~Node() {}

  NodeKind getKind() const { return Kind; }

  Value* getValue() { return V; }
  const Value* getValue() const { return V; }
  Instruction* getInst() { return dyn_cast<Instruction>(V); }
  const Instruction* getInst() const { return dyn_cast<Instruction>(V); }

  std::string name(void) const {
    if (getValue()->hasName())
      return std::string(getValue()->getName());
    return this->instType();
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

  unsigned distance(void) const {
    if (!isa<Instruction>(V))
      return -1;

    const Instruction *I = getInst();
    const BasicBlock *BB = I->getParent();
    int i = 0;

    for (auto &other : *BB){
      if (I == &other)
        return i;
      
      ++i;
    }

    std::string str = "Instruction not found on BasicBlock: ";
    llvm::raw_string_ostream rso(str);
    I->print(rso);
    llvm_unreachable(str.c_str());
  }

  friend llvm::raw_ostream& operator<<(llvm::raw_ostream& os, const Node &node){
    os << *(node.getValue());
    return os;
  }

  virtual void accept(Visitor &v) = 0;
};



class UnaryNode : public Node {
 public:
  Node *child;
  UnaryNode(Node *child, Instruction *I): child(child), Node(I, NK_UnaryNode){}
  UnaryNode(Node *child, Instruction *I, NodeKind Kind): child(child), Node(I, Kind){}

  MAKE_VISITABLE;
  MAKE_CLASSOF(NK_UnaryNode, NK_UnaryNode_End);
};

class CastNode : public UnaryNode {
 public:
  CastNode (Node *child, Instruction *I): UnaryNode(child, I, NK_CastNode){}

  MAKE_VISITABLE;
  MAKE_CLASSOF(NK_CastNode, NK_CastNode);
};

class StoreNode : public UnaryNode {
 public:
  StoreNode(Node *child, Instruction *I): UnaryNode(child, I, NK_StoreNode){
    assert(isa<phoenix::TargetOpNode>(child));
  }

  MAKE_VISITABLE;
  MAKE_CLASSOF(NK_StoreNode, NK_StoreNode);
};


class BinaryNode : public Node {
 public:
  Node *left;
  Node *right;

  BinaryNode(Node *left, Node *right, Instruction *I): left(left), right(right), Node(I, NK_BinaryNode){}
  BinaryNode(Node *left, Node *right, Instruction *I, NodeKind Kind): left(left), right(right), Node(I, Kind){}

  MAKE_VISITABLE;
  MAKE_CLASSOF(NK_BinaryNode, NK_BinaryNode_End);

};

class TargetOpNode : public BinaryNode {
 public:
  TargetOpNode(BinaryNode *binary, unsigned pos) : BinaryNode(binary->left, binary->right, binary->getInst(), NK_TargetOpNode){
    if (pos == SECOND)
      std::swap(left, right);

    assert (isa<LoadNode>(left) && "@left must be a LoadNode");
  }

  LoadNode* getLoad(void) const {
    return cast<LoadNode>(left);
  }

  Node* getOther(void) const {
    return right;
  }

  MAKE_VISITABLE;
  MAKE_CLASSOF(NK_TargetOpNode, NK_TargetOpNode);
};

class TerminalNode : public Node {
 public:
  TerminalNode(Value *V) : Node(V, NK_TerminalNode) {}
  TerminalNode(Value *V, NodeKind Kind) : Node(V, Kind) {}
  MAKE_VISITABLE;
  MAKE_CLASSOF(NK_TerminalNode, NK_TerminalNode_End);
};

class LoadNode : public TerminalNode {
 public:
  LoadNode(Value *V) : TerminalNode(V, NK_LoadNode){}
  
  MAKE_VISITABLE;
  MAKE_CLASSOF(NK_LoadNode, NK_LoadNode);
};

class ForeignNode : public TerminalNode {
 public:
  ForeignNode(Value *V) : TerminalNode(V, NK_ForeignNode){}

  MAKE_VISITABLE;
  MAKE_CLASSOF(NK_ForeignNode, NK_ForeignNode);
};

class ArgumentNode : public TerminalNode {
 public:
  ArgumentNode(Value *V) : TerminalNode(V, NK_ArgumentNode){}

  MAKE_VISITABLE;
  MAKE_CLASSOF(NK_ArgumentNode, NK_ArgumentNode);
};

class ConstantNode : public TerminalNode {
 public:
  ConstantNode(Constant *C) : TerminalNode(C, NK_ConstantNode){}
  ConstantNode(Constant *C, NodeKind Kind) : TerminalNode(C, Kind){}
  
  MAKE_VISITABLE;
  MAKE_CLASSOF(NK_ConstantNode, NK_ConstantNode);
};

class ConstantIntNode : public ConstantNode {
 public:
  ConstantIntNode(Constant *C) : ConstantNode(C, NK_ConstantIntNode){}

  std::string name(void) const {
    const ConstantInt *C = cast<ConstantInt>(getValue());
    return std::to_string(C->getSExtValue());
  }

  MAKE_VISITABLE;
  MAKE_CLASSOF(NK_ConstantIntNode, NK_ConstantIntNode);
};

}  // namespace phoenix
