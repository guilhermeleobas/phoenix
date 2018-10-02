#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Debug.h"        // To print error messages.
#include "llvm/ADT/Statistic.h"        // For the STATISTIC macro.
#include "llvm/IR/InstIterator.h"      // To use the iterator instructions(f)
#include "llvm/IR/Instructions.h"      // To have access to the Instructions.
#include "llvm/IR/Constants.h"         // For ConstantData, for instance.
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Support/raw_ostream.h"  // For dbgs()
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/IR/DebugInfoMetadata.h" // For DILocation
#include "llvm/Analysis/AliasAnalysis.h" // for alias analysis
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/Debug.h"
#include "llvm/IR/IntrinsicInst.h"

#include <iostream>
#include <fstream>
#include <map>
#include <stack>

#include "GEP.h"

#define DEBUG_TYPE "GEP"


std::map<Instruction*, bool> mapa;

const Function* findEnclosingFunc(const Value *V) {
  if (const Argument *Arg = dyn_cast<Argument>(V))
    return Arg->getParent();

  if (const Instruction *I = dyn_cast<Instruction>(V))
    return I->getParent()->getParent();

  return NULL;
}

const DILocalVariable* findVar(const Value *V, const Function *F) {
  for (auto Iter = inst_begin(F), End = inst_end(F); Iter != End; ++Iter) {
    const Instruction *I = &*Iter;
    if (const DbgDeclareInst *DbgDeclare = dyn_cast<DbgDeclareInst>(I)) {
      if (DbgDeclare->getAddress() == V)
        return DbgDeclare->getVariable();

    }
    else if (const DbgValueInst *DbgValue = dyn_cast<DbgValueInst>(I))
      if (DbgValue->getValue() == V)
        return DbgValue->getVariable();
  }
  return NULL;
}

StringRef getOriginalName(const Value *V) {
  const llvm::Function *F = findEnclosingFunc(V);
  if (!F) {
    return "";
  }
  const llvm::DILocalVariable *Var = findVar(V, F);
  if (!Var) {
    return "";
  }

  return Var->getName();
}

void GEP::print_instructions(Module &M){
  for (auto &F : M){
    errs() << F.getName() << "\n";
    for (auto &BB : F){
      for (auto &I : BB){
        errs() << '\t' << I << "\n";
      }
    }
  }
}

GetElementPtrInst* GEP::getElementPtr(Instruction* I, GetElementPtrInst *gep){

  std::set<Instruction*> visited;
  std::stack<Instruction*> s;

  if (!I)
    return nullptr;

  const BasicBlock *parent = I->getParent();

  s.push(I);
  visited.insert(I);

  while (!s.empty()){
    Instruction *I = s.top();
    s.pop();

    if (!I)
      continue;

    DEBUG(dbgs() << "[INS]: " << *I << "\n");

    // Compare if the instruction at the moment is a GEP instruction
    // if yes, compare if it has the same base pointer than `gep`
    if (GetElementPtrInst *other = dyn_cast<GetElementPtrInst>(I)){
      DEBUG(dbgs() << "[GEP]: " << *other << " \t " << *other->getPointerOperand() << "\n");
      DEBUG(dbgs() << "[GEP]: " << *gep << " \t " << *gep->getPointerOperand() << "\n");
      if (other->getPointerOperand() == gep->getPointerOperand())
        return other;
    }

    for (auto &op : I->operands()){
      Instruction *ins = dyn_cast<Instruction>(op);
      if (!ins)
        continue;
      const BasicBlock *BB = ins->getParent();
      if (visited.find(ins) == visited.end() ){
        visited.insert(ins);
        s.push(ins);
      }
    }
  }

  return nullptr;
}

bool GEP::runOnFunction(Function &F) {

  DEBUG(dbgs() << "[Function]: " << F.getName() << "\n");

  for (auto &BB : F){
    for (auto &I : BB){
      
      // if (StoreInst *store = dyn_cast<StoreInst>(&I)){
      //   store_count++;
      //   auto *I = dyn_cast<Instruction>(store->getPointerOperand());
      //   if (!I)
      //     continue;
      //   const std::string opcode = I->getOpcodeName();
      //   if (opcode == "getelementptr")
      //     eq_count++;
      // }
      // else if (LoadInst *load = dyn_cast<LoadInst>(&I)){
      //   store_count;
      //   auto *I = dyn_cast<Instruction>(load->getPointerOperand());
      //   if (!I)
      //     continue;
      //   const std::string opcode = I->getOpcodeName();
      //   if (opcode == "getelementptr")
      //     eq_count++;
      // }
      
      if (StoreInst *store = dyn_cast<StoreInst>(&I)){
        store_count++;

        DEBUG(dbgs() << "[STORE]: " << *store << "\n");

        GetElementPtrInst *ptr = dyn_cast<GetElementPtrInst>(store->getPointerOperand());
        if (!ptr)
          continue;
        GetElementPtrInst *val = getElementPtr(dyn_cast<Instruction>(store->getValueOperand()), ptr);

        if (val){
          eq_count++;
          // errs() << "Equals\n";
        }
      }
    }
  }

  std::ofstream myfile;
  myfile.open ("store_count.txt");
  myfile << eq_count << ", " << store_count << "\n";
  myfile.close();
  return true;
}

// void GEP::getAnalysisUsage(AnalysisUsage &AU) const {
// 	AU.addRequired<AAResultsWrapperPass>();
// 	AU.setPreservesAll();
// }

char GEP::ID = 0;
static RegisterPass<GEP> X("GEP",
    "GEP pattern a = a OP b");
