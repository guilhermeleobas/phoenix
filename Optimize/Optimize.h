#pragma once

using namespace llvm;

#include "../Identify/Geps.h"
#include "../Identify/Identify.h"

class Optimize : public BasicBlockPass{
private:

public:
  // Pass identifier, for LLVM's RTTI support:
  static char ID;

  bool runOnBasicBlock(BasicBlock &);

  void getAnalysisUsage(AnalysisUsage &AU) const;

  Optimize() : BasicBlockPass(ID) {}
  ~Optimize() {}
};
