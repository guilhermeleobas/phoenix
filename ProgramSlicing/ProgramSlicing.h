#pragma once

#include "../PDG/PDGAnalysis.h"
#include "../ProgramSlicing/ProgramSlicing.h"

namespace phoenix{

class ProgramSlicing {
 private:
  ProgramDependenceGraph *PDG;
  LoopInfo *LI;
  DominatorTree *DT;
  PostDominatorTree *PDT;

  void reset_analysis(Function *F);

  void set_entry_block(Function *F, Loop *L);
  void set_exit_block(Function *F, Loop *L);
  ///
  Loop* remove_loops_outside_chain(BasicBlock *BB);
  Loop* remove_loops_outside_chain(Loop *L, Loop *keep = nullptr);

 public:
  ProgramSlicing(LoopInfo *LI, DominatorTree *DT, PostDominatorTree *PDT, ProgramDependenceGraph *PDG)
      : LI(LI), DT(DT), PDT(PDT), PDG(PDG) {}

  /// Slice the program given the start point @V
  void slice(Function *F, Instruction *I);
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