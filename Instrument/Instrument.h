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
  unsigned get_id(const LoadInst*);
  
  /*
    Keep track of every memory access
  */
  void record_access(Module*, LoadInst*, Value*, const std::string&);
  void record_access(Module*, StoreInst*, Value*, const std::string&);

  
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
  
  
  // Inserts in the program a function call to dump a csv
  void insert_dump_call(Module*);
  void insert_dump_call(Module*, Instruction*);
  
  Instrument() : FunctionPass(ID) {}
  ~Instrument() { }
  
  private:
  
  GlobalVariable *gv_ts;
  std::map<const Value*, unsigned> IDs;
  bool go = false;
  std::map<const std::string, Value*> opcode_map;
  
};


