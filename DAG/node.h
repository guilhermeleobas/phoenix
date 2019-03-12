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

namespace phoenix {

std::string get_symbol(Instruction *I);
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

 protected:
  Value* getValue() const { return V; }
  Instruction* getInst() const { return dyn_cast<Instruction>(V); }

 public:
  Node(Value *V): V(V), Label() {}
  virtual ~Node() {}

  virtual void toDot(void) const = 0;
  virtual std::string toString(void) const = 0;
  virtual unsigned getHeight(void) const = 0;

  std::string name(void) const {
    return std::string(getValue()->getName());
  }

  std::string dump(void) const {
    std::string str;
    llvm::raw_string_ostream rso(str);
    getValue()->print(rso);
    return str;
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
  };
};



class UnaryNode : public Node {
 protected:
  Node *node;

 public:
  UnaryNode(Node *node, Instruction *I): node(node), Node(I){}

  void toDot() const override {
    std::string labelA = this->getLabel();
    std::string labelB = node->getLabel();
    errs() << labelA << " [label = " << this->name() << "]\n";
    errs() << labelA << " -> " << labelB << "\n";
    node->toDot();
  }

  std::string toString() const override {
    return get_symbol(getInst()) + "(" + node->toString() + ")";
  }

  Node* getNode(void) const { return node; }

  unsigned getHeight(void) const override { return 1 + node->getHeight(); }
};

class StoreNode : public UnaryNode {
 public:
  StoreNode(Node *node, Instruction *I) : UnaryNode(node, I){}

  void toDot() const override {
    std::string labelA = this->getLabel();
    std::string labelB = node->getLabel();
    errs() << labelA << " [label = store]\n";
    errs() << labelA << " -> " << labelB << "\n";
    node->toDot();
  }

  void dumpDebugInfo(void) const {
    errs() << *getInst() << " -> " << getFileName(getInst()) << "/" << std::to_string(getLineNo(getValue())) << "\n";
  }

  std::string toString(void) const override {
    return "\nStore -> " + node->name() + " = " + node->toString();
  }

  unsigned getHeight(void) const override {
    return node->getHeight();
  }
};

class BinaryNode : public Node {
 protected:
  Node *left;
  Node *right;

 public:
  BinaryNode(Node *left, Node *right, Instruction *I): left(left), right(right), Node(I){}

  void toDot() const override {
    std::string labelA = this->getLabel();
    std::string labelB = left->getLabel();
    std::string labelC = right->getLabel();

    errs() << labelA << " [label = \"" << this->name() << " = " 
           << "\\n" << get_symbol(getInst()) << "\"]\n";
    errs() << labelA << " -> " << labelB << "\n";
    errs() << labelA << " -> " << labelC << "\n";

    left->toDot();
    right->toDot();
  }

  std::string toString() const override {
    return "(" + left->toString() + " " + get_symbol(getInst()) + " " + right->toString() + ")";
  }

  unsigned getHeight(void) const override {
    return 1 + std::max(left->getHeight(), right->getHeight());
  }

  Node* getLeft(void) const { return left; }
  Node* getRight(void) const { return right; }
};

class CmpNode : public BinaryNode {
 public:
  CmpNode(Node *left, Node *right, Instruction *I) : BinaryNode(left, right, I){}
};

class TerminalNode : public Node {
 public:
  TerminalNode(Value *V) : Node(V) {}
  void toDot() const override {
    std::string labelA = this->getLabel();
    errs() << labelA << " [label = \"" << this->name() << "\\n" << this->instType() << "\"]\n";
  }

  std::string toString() const override {
    return name();
  }

  unsigned getHeight(void) const override { return 1; }
};

class LoadNode : public TerminalNode {
 public:
  LoadNode(Instruction *I): TerminalNode(I){}
};

class PHINode : public TerminalNode {
 public:
  PHINode(Instruction *I): TerminalNode(I){}
};

class SelectNode : public TerminalNode {
 public:
  SelectNode(Instruction *I) : TerminalNode(I){}
};

class ArgumentNode : public TerminalNode {
 public:
  ArgumentNode(Value *V) : TerminalNode(V){}

  std::string instType() const override {
    return "Argument";
  }
};

class ConstantNode : public TerminalNode {
 public:
  ConstantNode(Constant *C) : TerminalNode(C){}

  void toDot() const override {
    std::string labelA = this->getLabel();
    errs() << labelA << " [label = \"" << *getValue() << "\"]\n";
  }

  std::string toString() const override {
    std::string str;
    llvm::raw_string_ostream rso(str);
    getValue()->print(rso);
    return str;
  }
};

class ConstantIntNode : public ConstantNode {
 public:
  ConstantIntNode(Constant *C) : ConstantNode(C){}

  void toDot() const override{
    std::string value = "";
    value = std::to_string(dyn_cast<ConstantInt>(getValue())->getSExtValue());

    std::string labelA = this->getLabel();
    errs() << labelA << " [label = " << value << "]\n";
  }

  std::string toString() const override {
    return std::to_string(dyn_cast<ConstantInt>(getValue())->getSExtValue());
  }
};

unsigned getHeight(phoenix::Node *node);

}  // namespace phoenix