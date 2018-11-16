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

std::map<Instruction*, unsigned> Optimize::map_values(BasicBlock *BB){
  std::map<Instruction*, unsigned> mapa;
  for (BasicBlock::iterator i = BB->begin(), e = BB->end(); i != e; ++i){
    mapa[&*i] = mapa.size();
    // DEBUG(dbgs() << mapa[&*i] <<  " -> " << *i << "\n");
  }

  return mapa;
}

llvm::SmallVector<Instruction *, 10>
Optimize::mark_instructions_to_be_moved(StoreInst *store, std::map<Instruction*, unsigned> &mapa) {
  std::queue<Instruction *> q;
  llvm::SmallVector<Instruction *, 10> marked;

  for (Value *v : store->operands()) {
    if (Instruction *i = dyn_cast<Instruction>(v)) {
      q.push(i);
    }
  }

  marked.push_back(store);

  while (!q.empty()) {
    Instruction *v = q.front();
    q.pop();

    // Check if *v is only used in instructions already marked
    bool all_marked =
        std::all_of(begin(v->users()), end(v->users()), [&marked](Value *user) {
          return find(marked, user) != marked.end();
        });

    if (!all_marked){
      DEBUG(dbgs() << "-> Ignoring: " << *v << "\n");
      continue;
    }

    // Insert v in the list of marked values and its operands in the queue
    marked.push_back(v);

    if (User *u = dyn_cast<User>(v)) {
      for (Value *op : u->operands()) {
        if (Instruction *inst = dyn_cast<Instruction>(op)) {
          // restrict ourselves to instructions on the same basic block
          if (v->getParent() != inst->getParent()){
            DEBUG(dbgs() << "-> not in the same BB: " << *inst << "\n");
            continue;
          }

          if (isa<PHINode>(inst))
            continue;

          q.push(inst);
        }
      }
    }
  }

  // // sort the instructions
  // std::sort(marked.begin(), marked.end(), [&mapa](Instruction *a, Instruction *b){
  //   errs() << "a: " << *a << "\n";
  //   assert(mapa.find(a) != mapa.end() && "Error *a");
  //   assert(mapa.find(b) != mapa.end() && "Error *b");
  //   return mapa[a] > mapa[b];
  // });

  return marked;
}

void Optimize::move_marked_to_basic_block(
    llvm::SmallVector<Instruction *, 10> &marked, TerminatorInst *br) {
  for (Instruction *inst : reverse(marked)) {
    inst->moveBefore(br);
  }
}

void Optimize::insert_if(const Geps &g) {

  Value *v = g.get_v();
  Instruction *I = g.get_instruction();
  StoreInst *store = g.get_store_inst();
  IRBuilder<> Builder(store);

  std::map<Instruction*, unsigned> mapa = map_values(store->getParent());

  Value *idnt;
  Value *cmp;

  if (v->getType()->isFloatingPointTy()) {
    cmp = Builder.CreateFCmpONE(v, get_identity(I));
  } else {
    cmp = Builder.CreateICmpNE(v, get_identity(I));
  }

  TerminatorInst *br = llvm::SplitBlockAndInsertIfThen(
      cmp, dyn_cast<Instruction>(cmp)->getNextNode(), false);

  llvm::SmallVector<Instruction *, 10> marked =
    mark_instructions_to_be_moved(store, mapa);

  for_each(marked, [](Instruction *inst) {
    DEBUG(dbgs() << " Marked: " << *inst << "\n");
  });

  move_marked_to_basic_block(marked, br);
}

// This should check for cases where we can't insert an if. For instance,
//   I: *p = *p + 1
bool Optimize::can_insert_if(Geps &g) {
  // v is the operand that is not *p
  if (Constant *c = dyn_cast<Constant>(g.get_v())) {
    if (c != get_identity(g.get_instruction()))
      return false;

    // I truly believe that instCombine previously spotted and removed any
    // easy optimization involving identity, like:
    //  I: *p = *p + 0
    //  I: *p = *p * 1
    // I am writting the code below just for the peace of mind

    // We already know here that c == identity for the given operation.
    // Let's just compare if c is in the correct spot. For instance, we can't
    // optimize the instruction below.
    //  I: *p = 0 - *p

    unsigned v_pos = g.get_v_pos();
    Instruction *I = g.get_instruction();

    switch (I->getOpcode()) {
    case Instruction::Add:
    case Instruction::FAdd:
    case Instruction::Mul:
    case Instruction::FMul:
    case Instruction::Xor:
      // Commutativity. The pos of v doesn't matter!
      //  *p + v = v + *p
      //  *p * v = v * *p
      return true;
    case Instruction::Sub:
    case Instruction::FSub:
      // true only if v == SECOND:
      //  *p - v != v - *p
      return (v_pos == SECOND) ? true : false;
    // case Instruction::UDiv:
    // case Instruction::SDiv:
    // case Instruction::Shl:
    // case Instruction::LShr:
    // case Instruction::AShr:
    // case Instruction::And:
    // case Instruction::Or:
    default:
      std::string str = "Error (can_insert_if): ";
      llvm::raw_string_ostream rso(str);
      I->print(rso);
      llvm_unreachable(str.c_str());
    }
  }

  return true;
}

// This should implement a cost model
// Right now we only insert the `if` if the depth is >= threshold(1)
// TO-DO: Use a more sophisticated solution
bool Optimize::worth_insert_if(Geps &g) {
  if (g.get_loop_depth() >= threshold)
    return true;

  return false;
}

void print_gep(Geps &g) {
  Instruction *I = g.get_instruction();
  DEBUG(dbgs() << "I: " << *I << "\n");

  const DebugLoc &loc = I->getDebugLoc();
  if (loc) {
    auto *Scope = cast<DIScope>(loc.getScope());
    DEBUG(dbgs() << " - File: " << Scope->getFilename() << ":" << loc.getLine() << "\n");
  }
  DEBUG(dbgs() << " - Loop Depth:" << g.get_loop_depth() << "\n");
  DEBUG(dbgs() << " - v : " << *g.get_v() << "\n");
}

bool Optimize::runOnFunction(Function &F) {

  if (F.isDeclaration() || F.isIntrinsic() || F.hasAvailableExternallyLinkage())
    return true;

  Identify *Idn = &getAnalysis<Identify>();

  llvm::SmallVector<Geps, 10> gs = Idn->get_instructions_of_interest();

  // Let's give an id for each instruction of interest
  for (auto &g : gs) {
    Instruction *I = g.get_instruction();

    print_gep(g);

    // sanity check for vector instructions
    if (I->getOperand(0)->getType()->isVectorTy() ||
        I->getOperand(1)->getType()->isVectorTy())
      assert(0 && "Vector type");

    if (can_insert_if(g) && worth_insert_if(g)){
      insert_if(g);
    }

    DEBUG(dbgs() << "\n");
  }

  return false;
}

void Optimize::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<Identify>();
  AU.addRequired<LoopInfoWrapperPass>();
  AU.setPreservesAll();
}

char Optimize::ID = 0;
static RegisterPass<Optimize> X("Optimize", "Optimize pattern a = a OP b", false, false);
