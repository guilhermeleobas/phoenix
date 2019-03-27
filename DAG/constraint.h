#pragma once

#include <iostream>

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

  constraintValue() : type(Type::Null) {}

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

  friend llvm::raw_ostream& operator<<(llvm::raw_ostream& os, const constraintValue &c){
    if (c.type == constraintValue::Type::Null)
      os << "Null Type";
    else if (c.type == constraintValue::Type::Int)
      os << "Int: " << c.intValue;
    else
      os << "Double: " << c.doubleValue;

    return os;
  }

  inline std::string to_string() const {
    if (type == Type::Null)
      return "";
    else if (type == Type::Int)
      return std::to_string(intValue);
    else
      return std::to_string(doubleValue);
  }
};


class Constraint{
 private:
  constraintValue *c = nullptr;

 public:

  void setConstraint(constraintValue *c) {
    this->c = c;
  }

  constraintValue* getConstraint(void) const {
    return c;
  }

  bool hasConstraint(void) const {
    return c != nullptr;
  }

  std::string label() const {
    return (c != nullptr) ? c->to_string() : "";
  }
};