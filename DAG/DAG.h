#pragma once

using namespace llvm;

#include "../Identify/Geps.h"
#include "../Identify/Identify.h"
#include "node.h"
#include "parser.h"

class DAG : public FunctionPass{
private:

public:
  // Pass identifier, for LLVM's RTTI support:
  static char ID;

  bool runOnFunction(Function &);

  void getAnalysisUsage(AnalysisUsage &AU) const;

  DAG() : FunctionPass(ID) {}
  ~DAG() {}
};
