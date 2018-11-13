#include "llvm/ADT/Statistic.h" // For the STATISTIC macro.
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/IR/Constants.h"         // For ConstantData, for instance.
#include "llvm/IR/DebugInfoMetadata.h" // For DILocation
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h" // To use the iterator instructions(f)
#include "llvm/IR/Instructions.h" // To have access to the Instructions.
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h" // To print error messages.
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h" // For dbgs()
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include <queue>

#include "Optimize.h"

#define DEBUG_TYPE "Optimize"

Value *Optimize::get_identity(const Instruction *I) {
  switch (I->getOpcode()) {
  case Instruction::Add:
  case Instruction::Sub:
  case Instruction::Xor:
    return ConstantInt::get(I->getType(), 0);
  case Instruction::Mul:
    return ConstantInt::get(I->getType(), 1);
  case Instruction::FAdd:
  case Instruction::FSub:
    return ConstantFP::get(I->getType(), 0.0);
  case Instruction::FMul:
    return ConstantFP::get(I->getType(), 1.0);
  // case Instruction::UDiv:
  // case Instruction::SDiv:
  // case Instruction::Shl:
  // case Instruction::LShr:
  // case Instruction::AShr:
  // case Instruction::And:
  // case Instruction::Or:
  default:
    std::string str = "Instruction not supported: ";
    llvm::raw_string_ostream rso(str);
    I->print(rso);
    llvm_unreachable(str.c_str());
  }
}

llvm::SmallVector<Instruction*, 10> Optimize::mark_instructions_to_be_moved(StoreInst *init){
  std::queue<Instruction*> q;
  llvm::SmallVector<Instruction*, 10> marked;

  for(Value *v : init->operands()){
    if (Instruction *i = dyn_cast<Instruction>(v)){
      q.push(i);
    }
  }

  marked.push_back(init);

  while (!q.empty()){
    Instruction *v = q.front();
    q.pop();
    
    // Check if *v is only used in instructions already marked
    bool all_marked = std::all_of(begin(v->users()), end(v->users()), [&marked](Value *user){
      return find(marked, user) != marked.end();
    });

    if (!all_marked)
      continue;

    // Insert v in the list of marked values and its operands in the queue
    marked.push_back(v);

    if (User *u = dyn_cast<User>(v)){
      for (Value *op : u->operands()){
        if (Instruction *inst = dyn_cast<Instruction>(op))
          q.push(inst);
      }
    }
  }

  return marked;
}

void Optimize::move_marked_to_basic_block(llvm::SmallVector<Instruction*, 10> &marked, TerminatorInst *br){
  for (Instruction *inst : reverse(marked)){
    inst->moveBefore(br);
  }
}

void Optimize::insert_if(const Geps &g) {

  Value *v = g.get_v();
  // DEBUG(dbgs() << "v: " << *g.get_v() << "\n");
  Instruction *I = g.get_instruction();
  StoreInst *store = g.get_store_inst();
  IRBuilder<> Builder(store);

  Value *idnt;
  Value *cmp;

  if (v->getType()->isFloatingPointTy()) {
    cmp = Builder.CreateFCmpUNE(v, get_identity(I));
  } else {
    cmp = Builder.CreateICmpNE(v, get_identity(I));
  }

  TerminatorInst *br = llvm::SplitBlockAndInsertIfThen(
      cmp, dyn_cast<Instruction>(cmp)->getNextNode(), false);

  llvm::SmallVector<Instruction*, 10> marked = mark_instructions_to_be_moved(store);

  for_each(marked, [](Instruction *inst){
    DEBUG(dbgs() << "Marked: " << *inst << "\n");
  });

  move_marked_to_basic_block(marked, br);

  
  // DEBUG(dbgs() << "cmp: " << *cmp << "\n");
  // DEBUG(dbgs() << "Branch: " << *br << "\n");
}

// This should check for cases where we can't insert an if. For instance,
//   I: *p = *p + 1
bool Optimize::can_insert_if(Geps &g){
  // v is the operand that is not *p
  if (Constant *c = dyn_cast<Constant>(g.get_v())){
    if (c != get_identity(g.get_instruction()))
      return false;
  }

  return true;
}

// This should implement a cost model
// Ideas: Use something alongside loop depth
bool Optimize::worth_insert_if(Geps &g){

  return true;
}

bool Optimize::runOnFunction(Function &F) {

  if (F.isDeclaration() || F.isIntrinsic() || F.hasAvailableExternallyLinkage())
    return true;

  Identify *Idn = &getAnalysis<Identify>();

  llvm::SmallVector<Geps, 10> gs = Idn->get_instructions_of_interest();

  // Let's give an id for each instruction of interest
  for (auto &g : gs) {
    Instruction *I = g.get_instruction();

    DEBUG(dbgs() << "I: " << *I << " depth: " << g.get_loop_depth() << "\n");

    // sanity check for vector instructions
    if (I->getOperand(0)->getType()->isVectorTy() ||
        I->getOperand(1)->getType()->isVectorTy())
      assert(0 && "Vector type");

    if (can_insert_if(g) && worth_insert_if(g))
      insert_if(g);
  }

  return false;
}

void Optimize::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<Identify>();
  AU.addRequired<LoopInfoWrapperPass>();
  AU.setPreservesAll();
}

char Optimize::ID = 0;
static RegisterPass<Optimize> X("Optimize", "Optimize pattern a = a OP b");
