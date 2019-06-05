#pragma once

#include "../PDG/PDGAnalysis.h"

namespace phoenix{

class ProgramSlicing {
 private:
  ProgramDependenceGraph *PDG;
  LoopInfo *LI;
  DominatorTree *DT;
  PostDominatorTree *PDT;
  // DeadCodeElimination *DCE;

  ///
  void remove_dead_loops(Loop *parent, Loop *keep);
  void remove_loops_outside_chain(BasicBlock *BB);
  void remove_loops_outside_chain(Loop *L, Loop *keep = nullptr);

 public:
  ProgramSlicing(LoopInfo *LI, DominatorTree *DT, PostDominatorTree *PDT, ProgramDependenceGraph *PDG)
      : LI(LI), DT(DT), PDT(PDT), PDG(PDG) {}

  /// Slice the program given the start point @V
  void slice(Instruction *I);
};

class ProgramSlicingWrapperPass : public FunctionPass {
 private:
  ProgramSlicing *PS;

 public:
  // Pass identifier, for LLVM's RTTI support:
  static char ID;

  ProgramSlicing* getPS();

  bool runOnFunction(Function&);
  void getAnalysisUsage(AnalysisUsage& AU) const;

  ProgramSlicingWrapperPass() : FunctionPass(ID) {}
  ~ProgramSlicingWrapperPass() {}
};

};  // namespace phoenix