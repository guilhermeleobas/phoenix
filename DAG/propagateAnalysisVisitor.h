#pragma once

#include "visitor.h"
#include "constantWrapper.h"

// Converts a constant of value *v(T) from type T -> t
// This converts things such as float 0.0000 to int 0
// and the other way around
Value* convert(Value *v, Value *target){
  // errs() << "Converting " << *v << " -> " << *target << "(" << *target->getType() << ")" << "\n";
  if (!target->getType()->isFloatingPointTy() and !target->getType()->isIntegerTy())
    return nullptr;

  if (v->getType()->isFloatingPointTy()){
    // v => Float/Double
    if (target->getType()->isFloatingPointTy()){
      // target => Float/Double
      return ConstantFP::get(target->getType(), 0.0);
    }
    // Target => Int
    return ConstantInt::get(target->getType(), 0);
  }
  else {
    // v => Int
    if (target->getType()->isFloatingPointTy()){
      // Target => Float/Double
      return ConstantFP::get(target->getType(), 0.0);
    }
    // Target => Int
    return ConstantInt::get(target->getType(), 0);
  }
}

Value* getAbsorbing(Instruction *I){
  switch (I->getOpcode()) {
  case Instruction::Mul:
  case Instruction::UDiv:
  case Instruction::SDiv:
  case Instruction::And:
    return ConstantInt::get(I->getType(), 0);
  case Instruction::FMul:
  case Instruction::FDiv:
    return ConstantFP::get(I->getType(), 0.0);
  case Instruction::Or:
    return ConstantInt::get(I->getType(), 0xFFFFFFFF);
  case Instruction::Shl:
  case Instruction::LShr:
  case Instruction::AShr:
  case Instruction::Xor:
    return nullptr;

  default:
    // errs() << "No destructor for: " << *I << "\n";
    return nullptr;
    std::string str = "No destructor for: ";
    llvm::raw_string_ostream rso(str);
    I->print(rso);
    llvm_unreachable(str.c_str());
  }
}

Value* getIdentity(Instruction *I, const Geps *g) {
  switch (I->getOpcode()) {
  case Instruction::Add:
  case Instruction::Sub:
  case Instruction::Xor:
  //
  case Instruction::Shl:
  case Instruction::LShr:
  case Instruction::AShr:
  case Instruction::Or:
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

  case Instruction::And:
    // !0 is the identity of and 
    // and !0 is the absorbing element of OR
    return g->get_p_before();
  default:
    std::string str = "Instruction not supported: ";
    llvm::raw_string_ostream rso(str);
    I->print(rso);
    llvm_unreachable(str.c_str());
  }
}


class propagateAnalysisVisitor : public Visitor {
 public:
  Value *id = nullptr;

  propagateAnalysisVisitor(phoenix::StoreNode *store, const Geps *g) {
    phoenix::Node *child = store->child;
    id = getIdentity(child->getInst(), g);
    store->accept(*this);
  }

  void visit(phoenix::StoreNode *store) override {
    store->setConstant(id);
    store->child->accept(*this);
  }

  void visit(phoenix::UnaryNode *unary) override {

    // for a given unary instruction
    // one propagates the `id` iff the destructor
    // of the operand(unary) == id
    Value *des = getAbsorbing(unary->getInst());

    if (des == id){
      unary->setConstant(id);
      unary->child->accept(*this);
    }

  }

  void visit(phoenix::CastNode *cast) override {
    Instruction *I = cast->getInst();
    cast->setConstant(id);
    Value *conv = convert(id, cast->child->getValue());
    if (conv != nullptr){
      Value *other = id;
      id = conv;
      cast->child->accept(*this);
      id = other;
    }
  }

  void visit(phoenix::BinaryNode *binary) override {

    // The same is valid here! One only propagates `id` iff
    // id == destructor(binary_op);
    Value *des = getAbsorbing(binary->getInst());

    if (des == id){
      binary->setConstant(id);
      binary->left->accept(*this);
      binary->right->accept(*this);
    }

  }

  void visit(phoenix::TargetOpNode *target) override {
    
    Instruction *I = target->getInst();

    // switch(I->getOpcode()){
    //   case Instruction::Sub:
    //   case Instruction::FSub:
    //   case Instruction::Shl:
    //   case Instruction::LShr:
    //   case Instruction::AShr:
    //   case Instruction::UDiv:
    //   case Instruction::SDiv:
    //     if (isa<phoenix::LoadNode>(target->right)){
    //       errs() << "Retornou: " << *I << "\n";
    //       errs() << "left: " << *target->left << "\n";
    //       errs() << "right: " << *target->right << "\n";
    //       return;
    //     }
    // }

    target->setConstant(id);

    target->getOther()->accept(*this);
  }

  void visit(phoenix::TerminalNode *term) override {
    term->setConstant(id);
  }
  
  void visit(phoenix::LoadNode *load) override {
    visit(cast<phoenix::TerminalNode>(load));
  }

  void visit(phoenix::ForeignNode *f) override {
    return;
  }

  void visit(phoenix::ArgumentNode *a) override {
    return;
  }

  void visit(phoenix::ConstantNode *c) override {
    if (id == c->getValue())
      c->setConstant(id);
  }

  void visit(phoenix::ConstantIntNode *c) override {
    if (id == c->getValue())
      c->setConstant(id);
  }
};