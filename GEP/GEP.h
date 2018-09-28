#pragma once

using namespace llvm;

class GEP: public FunctionPass {
  public: 
  // Pass identifier, for LLVM's RTTI support:
  static char ID;

  bool runOnFunction(Function&);
  
  /*
   * Debugging method
   */
  void print_instructions(Module &M);
  
  GetElementPtrInst* getElementPtr(Instruction*, GetElementPtrInst*);

  // void getAnalysisUsage(AnalysisUsage &AU) const;

  GEP() : FunctionPass(ID), eq_count(0), store_count(0) {}
  ~GEP() { }
  
  private:
  
  unsigned int store_count, eq_count;
  
  
};


