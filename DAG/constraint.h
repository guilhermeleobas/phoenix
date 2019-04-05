#pragma once

class Constraint{
 private:
  Value *c = nullptr;

 public:

  void setConstraint(Value *c){
    this->c = c;

  }

  Value* getConstraint(void) const {
    return c;
  }

  bool hasConstraint(void) const {
    return c != nullptr;
  }

  std::string label() const {
    if (c == nullptr)
      return "";
    else if (c->getType()->isFloatingPointTy())
      return "0.0"; // To-Do: other identities!
    else
      return "0";
  }
};