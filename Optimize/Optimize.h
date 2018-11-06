#pragma once

using namespace llvm;

#include "../Identify/Geps.h"
#include "../Identify/Identify.h"

class Optimize : public FunctionPass{
private:

public:
  // Pass identifier, for LLVM's RTTI support:
  static char ID;

  bool runOnFunction(Function &);

  void getAnalysisUsage(AnalysisUsage &AU) const;

  Optimize() : FunctionPass(ID) {}
  ~Optimize() {}
};
