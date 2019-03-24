#pragma once

#include "visitor.h"

struct constraintValue {
  enum Type {
    Int,
    Double,
    Null
  };

  Type type;

  union {
    int intValue;
    double doubleValue;
  };

  constraintValue(){}

  constraintValue (int x) : type(Type::Int){
    intValue = x;
  }

  constraintValue (double x) : type(Type::Double){
    doubleValue = x;
  }

  bool operator==(const constraintValue &other) const {
    if (type != other.type)
      return false;
    
    if (type == Type::Int)
      return intValue == other.intValue;
    else
      return doubleValue == other.doubleValue;   
  }
};

constraintValue getDestructor(Instruction *I){
  switch (I->getOpcode()) {
  case Instruction::Mul:
  case Instruction::UDiv:
  case Instruction::SDiv:
  case Instruction::And:
    return constraintValue(0);
  case Instruction::FMul:
  case Instruction::FDiv:
    return constraintValue(0.0);
  default:
    break;
  }
}

constraintValue getIdentity(Instruction *I) {
  switch (I->getOpcode()) {
  case Instruction::Add:
  case Instruction::Sub:
  case Instruction::Xor:
  //
  case Instruction::Shl:
  case Instruction::LShr:
  case Instruction::AShr:
  case Instruction::And:
    return constraintValue(0);
    // return ::get(I->getType(), 0);
  //
  case Instruction::Mul:
  case Instruction::UDiv:
  case Instruction::SDiv:
    return constraintValue(1);
    // return ConstantInt::get(I->getType(), 1);
  //
  case Instruction::FAdd:
  case Instruction::FSub:
    return constraintValue(0.0);
    // return ConstantFP::get(I->getType(), 0.0);
  case Instruction::FMul:
  case Instruction::FDiv:
    return constraintValue(1.0);
    // return ConstantFP::get(I->getType(), 1.0);

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
  constraintValue id;

  ConstraintVisitor(const phoenix::UnaryNode *unary) {
    assert(isa<StoreInst>(unary->getInst()));
    constraintValue id = getIdentity(unary->child->getInst());
  }

  void visit(const phoenix::UnaryNode *unary) override {
    
  }

  void visit(const phoenix::BinaryNode *binary) override {

  }

  void visit(const phoenix::TerminalNode *term) override {

  }
};