#pragma once

using namespace llvm;

class Count : public ModulePass {
private:

public: 
  // Pass identifier, for LLVM's RTTI support:
  static char ID;

  bool runOnModule(Module&);
  
  /*
   * Debugging method
   */
  void print_instructions(Module &M);

  void insert_dump_call(Module &M, Instruction *I);
  void insert_dump_call(Module &M);
  
  // 
  void track(Module &M, Instruction *I, Value *op1, Value *op2);

  // Check if the instruction I is an arithmetic instruction
  // of interest. We don't instrument Floating-Point instructions
  // because they don't have an identity.
  bool is_arith_inst(Instruction *I);


  void getAnalysisUsage(AnalysisUsage &AU) const;
  
  Count() : ModulePass(ID){}
  ~Count() { }
};


