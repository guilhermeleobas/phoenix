#pragma once

#include "dependenceAnalysis.h"

using namespace llvm;

namespace phoenix {

class DependenceAnalysisWrapperPass : public FunctionPass {
 private:
  DependenceAnalysis* DA = nullptr;

 public:
  // Pass identifier, for LLVM's RTTI support:
  static char ID;

  bool runOnFunction(Function&);
  void getAnalysisUsage(AnalysisUsage& AU) const;

  DependenceAnalysis* getDependenceAnalysis();

  DependenceAnalysisWrapperPass() : FunctionPass(ID) {}
  ~DependenceAnalysisWrapperPass() {}
};

}  // end namespace phoenix