#pragma once

using namespace llvm;

class Count : public ModulePass {
  public: 
  // Pass identifier, for LLVM's RTTI support:
  static char ID;

  bool runOnModule(Module&);
  
  /*
   * Debugging method
   */
  void print_instructions(Module &M);
  
  Value* getElementPtr(Value*, std::set<Value*>*);
  
  Count() : ModulePass(ID) {}
  ~Count() { }
  
};


