#include "llvm/ADT/Statistic.h"               // For the STATISTIC macro.
#include "llvm/Analysis/DependenceAnalysis.h" // Dependency Analysis
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/IR/Constants.h"         // For ConstantData, for instance.
#include "llvm/IR/DebugInfoMetadata.h" // For DILocation
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h" // To use the iterator instructions(f)
#include "llvm/IR/Instructions.h" // To have access to the Instructions.
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"       // To print error messages.
#include "llvm/Support/raw_ostream.h" // For dbgs()
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include <fstream>
#include <map>
#include <queue>
#include <set>
#include <stack>

#include "Dfs.h"
#include "Profile.h"

#define DEBUG_TYPE "Profile"

void Profile::print_instructions(Module &M) {
  for (auto &F : M) {
    for (auto &BB : F) {
      for (auto &I : BB) {
        errs() << I << "\n";
      }
    }
  }
}

/*
 Create (if necessary) and return a unique ID for each load/store instruction
*/
void Profile::create_id(std::map<const Value *, unsigned> &IDs,
                        const Value *V) {
  IDs[V] = IDs.size();
}

void Profile::create_id(const Instruction *inst) {
  if (isa<LoadInst>(inst))
    create_id(load_ids, dyn_cast<const Value>(inst));
  else
    create_id(store_ids, dyn_cast<const Value>(inst));
}

unsigned Profile::get_id(std::map<const Value *, unsigned> &IDs,
                         const Value *V) {
  if (IDs.find(V) == IDs.end())
    IDs[V] = IDs.size();
  return IDs[V];
}

unsigned Profile::get_id(const Instruction *inst) {
  if (isa<LoadInst>(inst)) {
    return get_id(load_ids, dyn_cast<const Value>(inst));
  } else if (isa<StoreInst>(inst)) {
    return get_id(store_ids, dyn_cast<const Value>(inst));
  }
}

/*****************************************************************************/

/*
  For a given load of interest:
    %x = load %ptr
  Record the memory position being loaded (%ptr)
*/
void Profile::record_access(Module &M, StoreInst *I, Value *ptr,
                            const std::string &function_name = "record_store") {
  IRBuilder<> Builder(I);

  Constant *const_function = M.getOrInsertFunction(
      function_name, FunctionType::getVoidTy(M.getContext()),
      Type::getInt64Ty(M.getContext()),    // ID
      Type::getInt8PtrTy(M.getContext())); // Address

  Function *f = cast<Function>(const_function);

  Value *address =
      Builder.CreateBitCast(ptr, Type::getInt8PtrTy(M.getContext()));

  // Create the call
  std::vector<Value *> params;
  params.push_back(Builder.getInt64(get_id(I))), // ID
      params.push_back(address);                 // address
  CallInst *call = Builder.CreateCall(f, params);
}

/*
  For a given store of interest:
    store %val, %ptr
  Record the memory position %ptr whose %val is being written.
*/

void Profile::record_access(Module &M, LoadInst *I, Value *ptr,
                            const std::string &function_name = "record_load") {
  IRBuilder<> Builder(I);

  Constant *const_function = M.getOrInsertFunction(
      function_name, FunctionType::getVoidTy(M.getContext()),
      Type::getInt64Ty(M.getContext()),    // ID
      Type::getInt8PtrTy(M.getContext())); // Address

  Function *f = cast<Function>(const_function);

  Value *address =
      Builder.CreateBitCast(ptr, Type::getInt8PtrTy(M.getContext()));

  // Create the call
  std::vector<Value *> params;
  params.push_back(Builder.getInt64(get_id(I))); // id
  params.push_back(address);                     // address
  CallInst *call = Builder.CreateCall(f, params);
}

/*
  This function counts the number of visible stores. For a better
  understand of what a visible instruction is, check the paper below:

    Leobas, Guilherme V., Breno CF Guimarães, and Fernando MQ Pereira.
    "More than meets the eye: invisible instructions."
    Proceedings of the XXII Brazilian Symposium on Programming Languages. ACM,
  2018.
*/
void Profile::count_store(Module &M, StoreInst *I) {
  IRBuilder<> Builder(I);

  // Let's create the function call
  Constant *const_function = M.getOrInsertFunction(
      "count_store", FunctionType::getVoidTy(M.getContext()));

  Function *f = cast<Function>(const_function);

  // Create the call
  std::vector<Value *> args;
  Builder.CreateCall(f, args);
}

/*****************************************************************************/

/*
  Create a call to `dump_txt` function
*/
void Profile::insert_dump_call(Module &M, Instruction *I) {
  IRBuilder<> Builder(I);

  // Let's create the function call
  Constant *const_function = M.getOrInsertFunction(
      "dump_txt", FunctionType::getVoidTy(M.getContext()));

  Function *f = cast<Function>(const_function);

  // Create the call
  Builder.CreateCall(f, std::vector<Value *>());
}

/*
  Find the return inst and create call to `dump_txt`
*/
void Profile::insert_dump_call(Module &M) {
  for (auto &F : M) {
    for (auto &BB : F) {
      for (auto &I : BB) {
        if (ReturnInst *ri = dyn_cast<ReturnInst>(&I)) {
          if (F.getName() == "main")
            insert_dump_call(M, ri);
        } else if (CallInst *ci = dyn_cast<CallInst>(&I)) {
          Function *fun = ci->getCalledFunction();
          if (fun && fun->getName() == "exit") {
            insert_dump_call(M, ci);
          }
        }
      }
    }
  }
}

/*****************************************************************************/

void Profile::init_instrumentation(Module &M, const unsigned num_static_stores,
                                   const unsigned num_static_loads) {
  Function *F = M.getFunction("main");

  Instruction *ins = F->front().getFirstNonPHI();
  // Instruction *ins = F->front().getTerminator();

  IRBuilder<> Builder(ins);

  Constant *const_function = M.getOrInsertFunction(
      "init_instrumentation", FunctionType::getVoidTy(M.getContext()),
      Type::getInt64Ty(M.getContext()), // Number of static stores
      Type::getInt64Ty(M.getContext())  // Number of static loads
  );

  Function *f = cast<Function>(const_function);

  std::vector<Value *> args;
  args.push_back(Builder.getInt64(num_static_stores));
  args.push_back(Builder.getInt64(num_static_loads));

  CallInst *call = Builder.CreateCall(f, args);
}

/*****************************************************************************/

void Profile::mark_dependencies(Module &M) {

  // Create a unique ID for each load/store instruction
  // errs() << "Instructions:\n";
  for (auto &F : M) {
    for (auto inst = inst_begin(F); inst != inst_end(F); inst++) {
      if (isa<LoadInst>(*inst) || isa<StoreInst>(*inst)) {
        create_id(&*inst);
      }
    }
  }


  /*
    Map each store into its dependencies:
    store(0) -> load(1), load(3), load(xyz)
    store(1) -> load(0), load(1), ...
    and so on and so forth
  */
  std::map<int, std::vector<int>> dep;

  for (auto &F : M) {

    if (F.isDeclaration() || F.isIntrinsic() || F.hasAvailableExternallyLinkage())
      continue;

    // errs() << "Function: " << F.getName() << "\n";

    Dfs *D = &getAnalysis<Dfs>(F);

    for (auto src = inst_begin(F); src != inst_end(F); src++) {
      for (auto dest = src; dest != inst_end(F); dest++) {
        if (src == dest)
          continue;

        // Filter by Load->Store pairs
        if (!isa<LoadInst>(*src) || !isa<StoreInst>(*dest))
          continue;

        // errs() << "Testing: " << *src << " ->" << *dest << "\n";

        // check if there is a information flow from the load -> store
        std::vector<Flowpath> fps = D->get_all_paths(&*src, &*dest);

        if (fps.size()) {
          dep[get_id(&*dest)].push_back(get_id(&*src));
        }
      }
    }
  }

  std::ofstream f;
  f.open("map.txt");

  std::set<int> inst_used;

  for (auto &it : dep) {
    f << it.first << ' ' << it.second.size() << " ";
    stores_used.insert(it.first);
    for (auto &i : it.second) {
      loads_used.insert(i);
      f << i << " ";
    }
    f << "\n";
  }

  f.close();
}

/*****************************************************************************/

unsigned Profile::count_static_instances(Module &M, const unsigned opcode) {
  unsigned cnt = 0;

  for (auto &F : M)
    for (auto &BB : F)
      for (auto &I : BB)
        if (I.getOpcode() == opcode)
          cnt++;

  return cnt;
}

bool Profile::runOnModule(Module &M) {

  /*
    Mark all dependencies between loads and stores
    This is done to avoid false positives:
      v = *p;
      *p = 0;
    There is no dependency between the load and store above
  */
  mark_dependencies(M);

  /*
    Adds a call to init() and another to dump()
  */
  insert_dump_call(M);
  init_instrumentation(M, count_static_instances(M, Instruction::Store),
                       count_static_instances(M, Instruction::Load));

  for (auto &F : M) {
    for (auto &BB : F) {
      for (auto &I : BB) {
        if (StoreInst *store = dyn_cast<StoreInst>(&I)) {

          count_store(M, store);

          int store_id = get_id(store);
          if (stores_used.find(store_id) != stores_used.end())
            record_access(M, store, store->getPointerOperand(), "record_store");
        } else if (LoadInst *load = dyn_cast<LoadInst>(&I)) {
          int load_id = get_id(load);
          if (loads_used.find(load_id) != loads_used.end())
            record_access(M, load, load->getPointerOperand(), "record_load");
        }
      }
    }
  }

  return true;
}

void Profile::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<Dfs>();
  AU.setPreservesAll();
}

char Profile::ID = 0;
static RegisterPass<Profile> X("Profile", "Profile Binary Instructions");
