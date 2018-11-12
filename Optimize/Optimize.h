#pragma once

using namespace llvm;

#include "../Identify/Geps.h"
#include "../Identify/Identify.h"

class Optimize : public FunctionPass{
private:
  void insert_if(const Geps &g);
  Value* get_identity(const Instruction *I);
public:
  // Pass identifier, for LLVM's RTTI support:
  static char ID;

  bool runOnFunction(Function &);

  void getAnalysisUsage(AnalysisUsage &AU) const;

  Optimize() : FunctionPass(ID) {}
  ~Optimize() {}
};
