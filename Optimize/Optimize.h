#pragma once

using namespace llvm;

#include "../Identify/Geps.h"
#include "../Identify/Identify.h"

class Optimize : public FunctionPass{
private:

  unsigned threshold = 1;

  std::map<Instruction*, unsigned> map_values(BasicBlock *BB);

  void move_marked_to_basic_block(llvm::SmallVector<Instruction*, 10> &marked, TerminatorInst *br);
  llvm::SmallVector<Instruction *, 10>
  mark_instructions_to_be_moved(StoreInst *init,
                                std::map<Instruction *, unsigned> &mapa);
  void insert_if(const Geps &g);

  bool can_insert_if(Geps &g);
  bool worth_insert_if(Geps &g);
  
  Value* get_identity(const Instruction *I);
public:
  // Pass identifier, for LLVM's RTTI support:
  static char ID;

  bool runOnFunction(Function &);

  void getAnalysisUsage(AnalysisUsage &AU) const;

  Optimize() : FunctionPass(ID) {}
  ~Optimize() {}
};
