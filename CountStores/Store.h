#pragma once

using namespace llvm;

#include <map>
#include <set>

#include "../Identify/Geps.h"
#include "../Identify/Identify.h"


class Store : public ModulePass {
 private:
  std::map<StoreInst *, unsigned> mapa;
  std::set<StoreInst*> marked_stores;

  unsigned get_id(StoreInst *S);
  void assign_id(StoreInst *S);

  void dump();

 public:
  // Pass identifier, for LLVM's RTTI support:
  static char ID;

  bool runOnModule(Module &);

  void insert_dump_call(Module *M, Instruction *I);
  void insert_dump_call(Module *M);

  void create_call(Module *M,
                   StoreInst *S,
                   const StringRef &function_name,
                   Value *store_id,
                   Value *is_marked,
                   Value *cmp);
  void track_store(Module *M, StoreInst *S, unsigned store_id, bool is_marked);

  void getAnalysisUsage(AnalysisUsage &AU) const;

  Store() : ModulePass(ID) {}
  ~Store() {}
};
