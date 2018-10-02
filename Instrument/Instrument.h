#pragma once

using namespace llvm;

class Instrument : public FunctionPass {
  public: 
  // Pass identifier, for LLVM's RTTI support:
  static char ID;

  bool runOnFunction(Function&);
  
  /*
    Create and assign a unique ID to each LLVM::Value
    The second method is just a syntax sugar
  */
  unsigned get_id(const Value*);
  unsigned get_id(const Instruction*);
  
  /*
    Keep track of every memory access
  */
  void record_access(Module*, Instruction*, Value*, const std::string&);

  
  // Debugging method
  void print_instructions(Module &M);

  /*
    Create a global counter called "timestamp"
  */
  void create_counter(Module *M);
  
  /*
    Initialize the instrumentation code
  */
  void init_instrumentation(Module*);
  
  
  /*
    Alloc a global string pointer to load and store
  */
  // void alloc_opcode_to_string_ptr(Instruction*, const std::string&);
  // Value* get_opcode_string_ptr(Instruction*);

  // Inserts in the program a function call to dump a csv
  void insert_dump_call(Module*);
  void insert_dump_call(Module*, Instruction*);
  
  Instrument() : FunctionPass(ID) {}
  ~Instrument() { }
  
  private:
  
  GlobalVariable *gv_ts;
  std::map<const Value*, unsigned> IDs;
  std::map<const std::string, Value*> opcode_map;
  
};


