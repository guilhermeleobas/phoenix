#pragma once

#include "../PDG/PDGAnalysis.h"
#include "../ProgramSlicing/ProgramSlicing.h"

namespace phoenix{

class ProgramSlicing {
 private:

  void set_entry_block(Function *F, LoopInfo &LI, Loop *L);
  void set_exit_block(Function *F);
  void connect_basic_blocks(BasicBlock *to, BasicBlock *from);
  void connect_body_to_latch(BasicBlock *body, BasicBlock *latch);
  void connect_header_to_body(Loop *L, BasicBlock *body);
  ///
  void remove_subloops(Loop *L, Loop *marked);
  Loop* remove_loops_outside_chain(LoopInfo &LI, BasicBlock *BB);
  Loop* remove_loops_outside_chain(Loop *L, Loop *keep = nullptr);

 public:
  ProgramSlicing();

  /// Slice the program given the start point @V
  void slice(Function *F, Instruction *I);
};

};  // namespace phoenix