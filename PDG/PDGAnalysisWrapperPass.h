#pragma once

#include "PDGAnalysis.h"

using namespace llvm;

namespace phoenix {

class PDGAnalysisWrapperPass : public FunctionPass {
 private:
  PDG* DG = nullptr;

 public:
  // Pass identifier, for LLVM's RTTI support:
  static char ID;

  bool runOnFunction(Function&);
  void getAnalysisUsage(AnalysisUsage& AU) const;

  PDG* getPDG();

  PDGAnalysisWrapperPass() : FunctionPass(ID) {}
  ~PDGAnalysisWrapperPass() {}
};

}  // end namespace phoenix