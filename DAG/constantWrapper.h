#pragma once

class ConstantWrapper{
 private:
  Value *c = nullptr;

 public:

  void setConstant(Value *c){
    this->c = c;

  }

  Value* getConstant(void) const {
    return c;
  }

  bool hasConstant(void) const {
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