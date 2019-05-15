#pragma once

using namespace llvm;

#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/StringRef.h"
#include "../Identify/Geps.h"
#include "../Identify/Identify.h"
#include "node.h"
#include "NodeSet.h"
#include "parser.h"

class DAG : public FunctionPass {
 private:
  unsigned loop_threshold = 1;
  //
  LoopInfo *LI;
  DominatorTree *DT;
  Identify *Idtf;
  //
 
 private:

  bool worth_insert_if(Geps &g, unsigned loop_threshold);
  void run_dag_opt(Function &F);

  void split(StoreInst *store);
  void split(BasicBlock *BB);

  void update_passes(BasicBlock *from, BasicBlock *to);

 public:
  // Pass identifier, for LLVM's RTTI support:
  static char ID;

  bool runOnFunction(Function &);

  void getAnalysisUsage(AnalysisUsage &AU) const;

  DAG() : FunctionPass(ID) {}
  ~DAG() {}

};
