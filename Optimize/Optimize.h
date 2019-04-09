#pragma once

using namespace llvm;

#include "../Identify/Geps.h"
#include "../Identify/Identify.h"

class Optimize : public FunctionPass{
private:

  unsigned threshold = 1;

  void move_from_prev_to_then(BasicBlock *BBPrev, BasicBlock *BBThen);
  void move_from_prev_to_end(BasicBlock *BBPrev, BasicBlock *BBThen, BasicBlock *BBEnd);
  void move_marked_to_basic_block(llvm::SmallVector<Instruction*, 10> &marked, Instruction *br);
  llvm::SmallVector<Instruction *, 10> mark_instructions_to_be_moved(StoreInst *init);
  void insert_if(const Geps &g);

  bool worth_insert_if(Geps &g);
  bool filter_instructions(Geps &g);
  
  Value* get_identity(const Geps &g);
public:
  // Pass identifier, for LLVM's RTTI support:
  static char ID;

  bool runOnFunction(Function &);

  void getAnalysisUsage(AnalysisUsage &AU) const;

  Optimize() : FunctionPass(ID) {}
  ~Optimize() {}
};
