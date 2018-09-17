#pragma once

using namespace llvm;

class MyPass : public ModulePass {
  public: 
  // Pass identifier, for LLVM's RTTI support:
  static char ID;

  bool runOnModule(Module&);

  MyPass() : ModulePass(ID) {}
  ~MyPass() { }

};


