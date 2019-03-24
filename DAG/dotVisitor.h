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
#include <iostream>

using namespace llvm;

#include "visitor.h"

#define TAB "  "
#define ENDL "\\n"

#define DIGRAPH_BEGIN \
  "digraph G { \n"
#define DIGRAPH_END \
  "}\n";

#define QUOTE(s) \
  "\"" + s + "\""

#define NODE(id, l) \
  TAB + id + " [label = " + QUOTE(l) + "]"

#define NODECOLOR(id, l, color) \
  TAB + id \
  + " [label = " + QUOTE(l) + ", " \
  + "color = " + QUOTE(color) \
  + "]" \

#define EDGE(a, b) \
  TAB + a + " -> " + b

std::string get_symbol(Instruction *I) {
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
    default:
      return I->getOpcodeName();
      // std::string str = "Symbol not found: ";
      // llvm::raw_string_ostream rso(str);
      // I->print(rso);
      // llvm_unreachable(str.c_str());
  }
}


class DotVisitor : public Visitor {
 public:

  std::string str;

  void print(void) const {
    errs() << str << "\n";
  }

  void visit(const phoenix::UnaryNode *unary) override {
    Instruction *I = unary->getInst();
    phoenix::Node *child = unary->child;
    std::string labelA = unary->getLabel();
    std::string labelB = child->getLabel();

    if (isa<StoreInst>(I))
      str += DIGRAPH_BEGIN;

    if (isa<StoreInst>(I))
      str += NODE(labelA, "Store") + "\n";
    else
      str += NODE(labelA, unary->name()) + "\n";

    str += EDGE(labelA, labelB) + "\n";

    child->accept(*this);

    if (isa<StoreInst>(I))
      str += DIGRAPH_END;

  }

  void visit(const phoenix::BinaryNode *bin) override {
    phoenix::Node *left = bin->left, *right = bin->right;

    Instruction *I = bin->getInst();
    std::string labelA = bin->getLabel();
    std::string labelB = left->getLabel();
    std::string labelC = right->getLabel();

    str += NODE(labelA, bin->name() + " = " + ENDL + get_symbol(I)) + "\n";
    str += EDGE(labelA, labelB) + "\n";
    str += EDGE(labelA, labelC) + "\n";

    left->accept(*this);
    right->accept(*this);
  }

  void visit(const phoenix::TerminalNode *t) override {
    std::string labelA = t->getLabel();
    str += NODE(labelA, t->name()) + "\n";
  }

  void visit(const phoenix::ForeignNode *f) override {
    std::string labelA = f->getLabel();
    str += NODECOLOR(labelA, f->name(), "red") + "\n";
  }

};