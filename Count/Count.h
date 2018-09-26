#pragma once

using namespace llvm;

class Count : public FunctionPass {
  public: 
  // Pass identifier, for LLVM's RTTI support:
  static char ID;

  bool runOnFunction(Function&);
  
  /*
   * Debugging method
   */
  void print_instructions(Module &M);
  
  bool getMemoryUse(const MemorySSA&, const MemoryAccess*, Instruction*);
  
  Value* getElementPtr(Value*, std::set<Value*>*, int);

  void getAnalysisUsage(AnalysisUsage &AU) const;
  
  Count() : FunctionPass(ID), eq_count(0), store_count(0) {}
  ~Count() { }
  
  private:
  
  unsigned int store_count, eq_count;
  
  
};


