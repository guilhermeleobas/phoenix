#pragma once

using namespace llvm;

class Instrument : public ModulePass {
  public: 
  // Pass identifier, for LLVM's RTTI support:
  static char ID;

  bool runOnModule(Module&);

  /*
    Inserts in the program a function call to dump a csv
  */
  void insert_dump_call(Module &M, Instruction *I);
  
  /*
   * return the identity element given the opcode 
   */
  void init_group_identity(void);
  
  /*
    Debugging method
  */
  void print_instructions(Module &M);

  /*
    Allocate in the stack a new char* with the opcodeName of the 
    instruction. For instance, if the instruction is:
    ` store i32 %n, i32* %n.addr, align 4 `
    This function will create the following instruction:
    ` @0 = private unnamed_addr constant [6 x i8] c"store" `
  */
  Value* alloc_string(Instruction *I);
  Value* alloc_counter(Module &M, Instruction *I);

  /*
    Add an external call to @count_instruction.
    @param Module is self-explanatory
    @param Instruction is the instruction we want to count
    `count_instruction` is defined in the file Collect/collect.c
  */
  void insert_inc(Module &M, Instruction *inst);

  Instrument() : ModulePass(ID) {}
  ~Instrument() { }

};


