#pragma once

#include "../PDG/PDGAnalysis.h"
#include "../ProgramSlicing/ProgramSlicing.h"

namespace phoenix{

struct PathEdge {
  BasicBlock *from;
  BasicBlock *to;

  PathEdge() = delete;
  PathEdge(BasicBlock *from, BasicBlock *to) : from(from), to(to){}
};



class ProgramSlicing {
 private:

  void set_exit_block(Function *F);


 public:
  ProgramSlicing();

  /// Slice the program given the start point @I
  void slice(Function *F, Instruction *I);
};

};  // namespace phoenix