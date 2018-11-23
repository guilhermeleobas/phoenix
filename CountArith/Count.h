#pragma once

using namespace llvm;

#include <map>
#include "../Identify/Geps.h"
#include "../Identify/Identify.h"

using std::map;

class Count : public ModulePass {
private:
  map<Instruction*, unsigned> mapa;
  map<unsigned, unsigned> opcode_counter;
  
  unsigned get_id(Instruction *I);
  void assign_id(Instruction *I);

  void dump();

public:
  // Pass identifier, for LLVM's RTTI support:
  static char ID;

  bool runOnModule(Module &);

  void insert_dump_call(Module &M, Instruction *I);
  void insert_dump_call(Module &M);

  // Assign a unique ID to each arithmetic instruction of the type:
  //  - Add, Sub, Mul, Xor

  // Adds a function call to a function defined externally which checks
  // if op1 or op2 are the identity value of the instruction I
  // Instruction I is **always** an arithmetic instruction
  void track_int(Module &M, Instruction *I, Value *op1, Value *op2, Geps &g);
  void track_float(Module &M, Instruction *I, Value *op1, Value *op2, Geps &g);

  void getAnalysisUsage(AnalysisUsage &AU) const;

  Count() : ModulePass(ID) {}
  ~Count() {}
};
