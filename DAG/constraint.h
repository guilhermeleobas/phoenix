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
};

class Constraint {
 private:
  constraintValue cons;

 public:
  Constraint(){}

  void setConstraint(const constraintValue &c){
    cons = c;
  }

  std::string color() const{
    if (cons.type == constraintValue::Type::Null)
      return "black";
    
    return "blue";
  }

  std::string label(void) const {
    if (cons.type == constraintValue::Type::Null)
      return "";
    else if (cons.type == constraintValue::Type::Int)
      return std::to_string(cons.intValue);
    else
      return std::to_string(cons.doubleValue);
  }
};