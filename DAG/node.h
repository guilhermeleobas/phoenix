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

using namespace llvm;

namespace phoenix {

static int ID = 0;

inline int get_id() {
  return ID++;
}

inline std::string get_symbol(Instruction *I) {
  switch (I->getOpcode()) {
    case Instruction::Add:
      return "+";
    case Instruction::FAdd:
      return "+.";
    case Instruction::Sub:
      return "-";
    case Instruction::FSub:
      return "-.";
    case Instruction::Xor:
      return "^";
    case Instruction::Shl:
      return "<<";
    case Instruction::LShr:
    case Instruction::AShr:
      return ">>";
    case Instruction::Mul:
      return "*";
    case Instruction::FMul:
      return "*.";
    case Instruction::UDiv:
    case Instruction::SDiv:
      return "/";
    case Instruction::FDiv:
      return "/.";
    case Instruction::And:
      return "&";
    case Instruction::Or:
      return "|";
    case Instruction::SIToFP:
      return "sitofp";
    default:
      return I->getOpcodeName();
      std::string str = "Symbol not found: ";
      llvm::raw_string_ostream rso(str);
      I->print(rso);
      llvm_unreachable(str.c_str());
  }
}

class Node {
 public:
  Node() {}
  virtual void to_dot(void) const = 0;
  virtual std::string name(void) const = 0;
  virtual std::string toString(void) const = 0;
  virtual ~Node() {}
};

class ConstantNode : public Node {
 protected:
  Constant *C;

 public:
  ConstantNode(Constant *C) : C(C) {}

  void to_dot() const override {}
  std::string name() const override { return C->getName(); }
  std::string toString() const override { return "Cnt"; }
};

class ConstantIntNode : public ConstantNode {
 public:
  ConstantIntNode(Constant *C) : ConstantNode(C) {}

  std::string toString() const override {
    return std::to_string(dyn_cast<ConstantInt>(C)->getSExtValue());
  }
};

class UnaryNode : public Node {
 protected:
  Instruction *I;
  Node *node;

 public:
  UnaryNode(Node *node, Instruction *I) {
    this->node = std::move(node);
    this->I = I;
  }

  std::string name() const override { return std::string(I->getName()); }

  void to_dot() const override {
    errs() << I->getName() << " -> " << node->name() << ";"
           << "\n";
  }

  std::string toString() const override { return get_symbol(I) + "(" + node->toString() + ")"; }
};

class BinaryNode : public Node {
 protected:
  Node *left;
  Node *right;
  Instruction *I;

 public:
  BinaryNode(Node *left, Node *right, Instruction *I) {
    this->left = left;
    this->right = right;
    this->I = I;
  }

  void to_dot() const override {
    std::string symbol = "\"" + get_symbol(I) + std::to_string(get_id()) + "\"";
    errs() << I->getName() << " -> " << symbol << ";\n";
    errs() << symbol << " -> " << left->name() << ";"
           << "\n";
    errs() << symbol << " -> " << right->name() << ";"
           << "\n";

    left->to_dot();
    right->to_dot();
  }

  std::string name() const override { return std::string(I->getName()); }

  std::string toString() const override {
    return "(" + left->toString() + " " + get_symbol(I) + " " + right->toString() + ")";
  }
};

class CmpNode : public BinaryNode {
 public:
  CmpNode(Node *left, Node *right, Instruction *I) : BinaryNode(left, right, I){}
  std::string toString(void) const {
    return "Cmp(" + left->toString() + "," + right->toString() + ")";
  }
};

class TerminalNode : public Node {
 protected:
  Value *I;

 public:
  TerminalNode(Instruction *I) : I(I) {}
  TerminalNode(Value *V) : I(V) {}
  void to_dot() const override {}
  std::string name() const override { return I->getName(); }
  std::string toString() const override { return I->getName(); }
};

class MemoryNode : public TerminalNode {
 public:
  MemoryNode(Instruction *I): TerminalNode(I){}
};

class PHINode : public TerminalNode {
 public:
  PHINode(Instruction *I): TerminalNode(I){}

  std::string toString() const override { return std::string(I->getName()) + "[PHI]"; }
};

























}  // namespace phoenix