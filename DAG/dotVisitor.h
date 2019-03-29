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
#include <map>

using namespace llvm;

#include "visitor.h"
#include "constraint.h"


#define TAB "  "
#define ENDL "\\n"

#define DIGRAPH_BEGIN \
  "digraph G { \n"
#define DIGRAPH_END \
  "}\n";

#define QUOTE(s) \
  "\"" + s + "\""

#define NODE(id, l, color) \
  TAB + id \
  + " [label = " + QUOTE(l) + ", " \
  + "color = " + QUOTE(color) \
  + "]" \

#define EDGE(a, b, label, color) \
  TAB + a + " -> " + b \
  + " [label = " + QUOTE(label) + ", " \
  + " color = " + QUOTE(color) \
  + "]" \

#define COLOR(node) \
  (node->hasConstraint() ? "blue" : "black")

#define ID(node) \
  (std::to_string(node->getID()))


class DotVisitor : public Visitor {
 private:
  std::string str;

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

 public:
  
  DotVisitor(phoenix::StoreNode *store){
    store->accept(*this);
  }

  void print(void) const {
    errs() << str << "\n";
  }

  void visit(phoenix::StoreNode *store) override {
    phoenix::Node *child = store->child;
    std::string idA = ID(store);
    std::string idB = ID(child);

    str += DIGRAPH_BEGIN;

    str += NODE(idA, store->name(), COLOR(store)) + "\n";
    str += EDGE(idA, idB, "", COLOR(store)) + "\n";

    child->accept(*this);

    str += DIGRAPH_END;
  }

  void visit(phoenix::UnaryNode *unary) override {
    phoenix::Node *child = unary->child;
    std::string idA = ID(unary);
    std::string idB = ID(child);

    str += NODE(idA, unary->name(), COLOR(unary)) + "\n";
    str += EDGE(idA, idB, unary->label(), COLOR(unary)) + "\n";

    child->accept(*this);
  }

  void visit(phoenix::BinaryNode *binary) override {
    phoenix::Node *left = binary->left, *right = binary->right;

    Instruction *I = binary->getInst();
    std::string idA = ID(binary);
    std::string idB = ID(left);
    std::string idC = ID(right);

    str += NODE(idA, binary->name() + " = " + ENDL + get_symbol(I), COLOR(binary)) + "\n";
    str += EDGE(idA, idB, binary->label(), COLOR(binary)) + "\n";
    str += EDGE(idA, idC, binary->label(), COLOR(binary)) + "\n";

    left->accept(*this);
    right->accept(*this);
  }

  void visit(phoenix::TargetOpNode *target) override {
    phoenix::Node *other = target->getOther();
    phoenix::LoadNode *load = target->getLoad();

    Instruction *I = target->getInst();
    std::string idA = ID(target);
    std::string idB = ID(load);
    std::string idC = ID(other);

    str += NODE(idA, target->name() + " = " + ENDL + get_symbol(I), COLOR(target)) + "\n";
    str += EDGE(idA, idB, "", "black") + "\n";
    str += EDGE(idA, idC, target->label(), COLOR(target)) + "\n";

    load->accept(*this);
    other->accept(*this);
  }

  void visit(phoenix::TerminalNode *t) override {
    std::string labelA = ID(t);
    str += NODE(labelA, t->name(), COLOR(t)) + "\n";
  }

  void visit(phoenix::LoadNode *t) override {
    std::string labelA = ID(t);
    str += NODE(labelA, "Load " + t->name(), COLOR(t)) + "\n";
  }

  void visit(phoenix::ForeignNode *f) override {
    std::string labelA = ID(f);
    str += NODE(labelA, f->name(), "red") + "\n";
  }
  
  void visit(phoenix::ConstantNode *cnt) override {
    visit(cast<phoenix::TerminalNode>(cnt));
  }

  void visit(phoenix::ConstantIntNode *cnt) override {
    visit(cast<phoenix::TerminalNode>(cnt));
  }

};