#pragma once

#include "visitor.h"
#include "constraint.h"

Value* getDestructor(Instruction *I){
  switch (I->getOpcode()) {
  case Instruction::Mul:
  case Instruction::UDiv:
  case Instruction::SDiv:
  case Instruction::And:
    return ConstantInt::get(I->getType(), 0);
    // return constraintValue(0);
  case Instruction::FMul:
  case Instruction::FDiv:
    return ConstantFP::get(I->getType(), 0.0);
    // return constraintValue(0.0);
  default:
    errs() << "No destructor for: " << *I << "\n";
    return nullptr;
    // return constraintValue();
    std::string str = "No destructor for: ";
    llvm::raw_string_ostream rso(str);
    I->print(rso);
    llvm_unreachable(str.c_str());
  }
}

Value* getIdentity(Instruction *I) {
  switch (I->getOpcode()) {
  case Instruction::Add:
  case Instruction::Sub:
  case Instruction::Xor:
  //
  case Instruction::Shl:
  case Instruction::LShr:
  case Instruction::AShr:
  case Instruction::And:
    return ConstantInt::get(I->getType(), 0);
  //
  case Instruction::Mul:
  case Instruction::UDiv:
  case Instruction::SDiv:
    return ConstantInt::get(I->getType(), 1);
  //
  case Instruction::FAdd:
  case Instruction::FSub:
    return ConstantFP::get(I->getType(), 0.0);
  case Instruction::FMul:
  case Instruction::FDiv:
    return ConstantFP::get(I->getType(), 1.0);

  // case Instruction::Or:
  //   return g.get_p_before();
  default:
    std::string str = "Instruction not supported: ";
    llvm::raw_string_ostream rso(str);
    I->print(rso);
    llvm_unreachable(str.c_str());
  }
}


class ConstraintVisitor : public Visitor {
 public:
  // constraintValue id;
  Value *id = nullptr;

  ConstraintVisitor(phoenix::StoreNode *store) {
    phoenix::Node *child = store->child;
    id = getIdentity(child->getInst());
    store->accept(*this);
  }

  void visit(phoenix::StoreNode *store) override {
    store->setConstraint(id);
    store->child->accept(*this);
  }

  void visit(phoenix::UnaryNode *unary) override {

    // for a given unary instruction
    // one propagates the `id` iff the destructor
    // of the operand(unary) == id
    Value *des = getDestructor(unary->getInst());

    if (des == id){
      unary->setConstraint(id);
      unary->child->accept(*this);
    }

  }

  void visit(phoenix::BinaryNode *binary) override {

    // The same is valid here! One only propagates `id` iff
    // id == destructor(binary_op);
    Value *des = getDestructor(binary->getInst());

    if (des == id){
      binary->setConstraint(id);
      binary->left->accept(*this);
      binary->right->accept(*this);
    }

  }

  void visit(phoenix::TargetOpNode *target) override {
    target->setConstraint(id);
    target->getOther()->accept(*this);
  }

  void visit(phoenix::TerminalNode *term) override {
    term->setConstraint(id);
  }
  
  void visit(phoenix::LoadNode *load) override {
    visit(cast<phoenix::TerminalNode>(load));
  }

  void visit(phoenix::ForeignNode *f) override {
    return;
  }

  void visit(phoenix::ConstantNode *c) override {
    visit(cast<phoenix::TerminalNode>(c));
  }

  void visit(phoenix::ConstantIntNode *c) override {
    visit(cast<phoenix::TerminalNode>(c));
  }
};