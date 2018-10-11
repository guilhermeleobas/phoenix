#pragma once

using namespace llvm;

class Instrument : public ModulePass {
  public: 
  // Pass identifier, for LLVM's RTTI support:
  static char ID;

  bool runOnModule(Module&);

  /*
    Create a unique ID for each load/store
  */
  void create_id(std::map<const Value*, unsigned> &IDs, const Value *v);
  void create_id(const Instruction *inst);
  
  /*
    Create and assign a unique ID to each LLVM::Value
    The second method is just a syntax sugar
  */
  unsigned get_id(std::map<const Value*, unsigned> &IDs, const Value*);
  unsigned get_id(const Instruction*);
  
  /*
    Keep track of every memory access
  */
  void record_access(Module &M, LoadInst*, Value*, const std::string&);
  void record_access(Module &M, StoreInst*, Value*, const std::string&);
  void count_store (Module &M, StoreInst*);

  
  // Debugging method
  void print_instructions(Module &M);

  /*
    Create a global counter called "timestamp"
  */
  void create_counter(Module &M);
  
  /*
    Initialize the instrumentation code
  */
  void init_instrumentation(Module &M, const unsigned num_static_stores, const unsigned num_static_loads);

  /*
    Count the number of static instances for a given opcode
  */
  unsigned count_static_instances(Module &M, const unsigned opcode);
  
  
  // Inserts in the program a function call to dump a csv
  void insert_dump_call(Module &M);
  void insert_dump_call(Module &M, Instruction*);

  //
  bool is_arithmetic_inst(const Instruction*);
  bool dfs(const Instruction *source, const Instruction *dest,
           std::set<std::pair<const Instruction *, bool>> &visited, bool valid);
  void mark_dependencies(Module &M);


  void getAnalysisUsage(AnalysisUsage &AU) const;
  
  Instrument() : ModulePass(ID) {}
  ~Instrument() { }
  
  private:
  
  GlobalVariable *gv_ts; // timestamp

  std::map<const Value*, unsigned> load_ids;
  std::map<const Value*, unsigned> store_ids;

  std::set<int> stores_used, loads_used;
  
  std::map<const std::string, Value*> opcode_map;
  
};


