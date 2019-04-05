#pragma once

class Constraint{
 private:
  Value *v = nullptr;

 public:

  void setConstraint(Value *v){
    this->v = v;

  }

  Value* getConstraint(void) const {
    return v;
  }

  bool hasConstraint(void) const {
    return v != nullptr;
  }

  std::string label() const {
    if (v == nullptr)
      return "";
    else if (v->getType()->isFloatingPointTy())
      return "0.0"; // To-Do: other identities!
    else
      return "0";
  }
};