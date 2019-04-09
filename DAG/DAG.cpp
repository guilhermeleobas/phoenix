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

#include "DAG.h"
#include "dotVisitor.h"
#include "constraintVisitor.h"
#include "depthVisitor.h"

#define DEBUG_TYPE "DAG"

Value* DAG::get_identity(const Geps &g) {
  Instruction *I = g.get_instruction();
  switch (I->getOpcode()) {
  case Instruction::Add:
  case Instruction::Sub:
  case Instruction::Xor:
  //
  case Instruction::Shl:
  case Instruction::LShr:
  case Instruction::AShr:
    return ConstantInt::get(I->getType(), 0);
  //
  case Instruction::Mul:
  case Instruction::UDiv:
  case Instruction::SDiv:
    return ConstantInt::get(I->getType(), 1);
  //
  case Instruction::FAdd:
  case Instruction::FSub:
    return ConstantFP::get(I->getType(), 0.0);
  case Instruction::FMul:
  case Instruction::FDiv:
    return ConstantFP::get(I->getType(), 1.0);

  case Instruction::And:
  case Instruction::Or:
    return g.get_p_before();
  default:
    std::string str = "Instruction not supported: ";
    llvm::raw_string_ostream rso(str);
    I->print(rso);
    llvm_unreachable(str.c_str());
  }
}

llvm::SmallVector<Instruction *, 10>
DAG::mark_instructions_to_be_moved(StoreInst *store) {
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

  return marked;
}

void DAG::move_marked_to_basic_block(
    llvm::SmallVector<Instruction *, 10> &marked, Instruction *br) {
  for (Instruction *inst : reverse(marked)) {
    inst->moveBefore(br);
  }
}

void DAG::move_from_prev_to_then(BasicBlock *BBPrev, BasicBlock *BBThen){
  llvm::SmallVector<Instruction*, 10> list;

  for (BasicBlock::reverse_iterator b = BBPrev->rbegin(), e = BBPrev->rend(); b != e; ++b){
    Instruction *I = &*b;

    if (isa<PHINode>(I) || isa<BranchInst>(I))
      continue;

    if (begin(I->users()) == end(I->users()))
      continue;

    // Move I from BBPrev to BBThen iff all users of I are in BBThen
    //
    // Def 1.
    //  Program P is in SSA form. Therefore, I dominates all its users
    // Def 2. 
    //  We are iterating from the end of BBPrev to the beginning. Thus, this gives us the guarantee
    //  that all users of I living in the same BB were previously visited.

    bool can_move_I = std::all_of(begin(I->users()), end(I->users()), [&BBThen](User *U){
      BasicBlock *parent = dyn_cast<Instruction>(U)->getParent();
      return (parent == BBThen);
    });

    if (can_move_I){
      DEBUG(dbgs() << "[BBPrev -> BBThen] " << *I << "\n");
      --b;
      I->moveBefore(BBThen->getFirstNonPHI());
    }
  }
}

void DAG::move_from_prev_to_end(BasicBlock *BBPrev, BasicBlock *BBThen, BasicBlock *BBEnd){
  llvm::SmallVector<Instruction*, 10> marked;

  for (BasicBlock::reverse_iterator b = BBPrev->rbegin(), e = BBPrev->rend(); b != e; ++b){
    Instruction *I = &*b;

    if (isa<PHINode>(I) || isa<BranchInst>(I))
      continue;

    if (begin(I->users()) == end(I->users()))
      continue;

    // Move I from BBPrev to BBEnd iff all users of I are in BBEnd. 
    // 
    // Def 1. 
    //  The program is in SSA form
    // Def 2.
    //  We are iterating from the end of BBPrev to the beginning. 
    // 
    // Theorem: 
    //  Given an I that **has only users on BBend**, moving it from BBPrev -> BBEnd doesn't
    //  change the semanthics of the program P.
    // 
    // Proof:
    //  Let's assume that moving I from BBPrev to BBEnd changes the semantics of P. If it changes,
    //  there an instruction J that depends on I (is a user of I) and will be executed before I. There
    //  are two cases that can happen:
    //    J is in BBPrev: Because the program is in SSA form, I must dominate J. Therefore, to move I,
    //      we had to move J before to BBEnd. 
    //    J is in BBThen: Invalid. We don't move I if there is a user of it outside BBPrev.
    //
    // NOTE: THIS PROOF IS COMPLETELY WRONG!!! WE HAVE MEMORY ALIASING!!!!
    

    bool can_move_I = std::all_of(begin(I->users()), end(I->users()), [&BBPrev, &BBThen, &BBEnd](User *U){
      BasicBlock *BB = dyn_cast<Instruction>(U)->getParent();
      return (BB != BBPrev) && (BB != BBThen);
    });

    if (can_move_I){
      DEBUG(dbgs() << "[BBPrev -> BBEnd]" << *I << "\n");
      --b;
      I->moveBefore(BBEnd->getFirstNonPHI());
    }
  }
}

void DAG::insert_if(const Geps &g, const phoenix::Node *node) {

  Value *v = node->getValue();
  // Value *v = g.get_v();
  StoreInst *store = g.get_store_inst();
  IRBuilder<> Builder(store);

  Value *cmp;

  if (v->getType()->isFloatingPointTy()) {
    cmp = Builder.CreateFCmpONE(v, node->getConstraint());
  } else {
    cmp = Builder.CreateICmpNE(v, node->getConstraint());
  }

  TerminatorInst *br = llvm::SplitBlockAndInsertIfThen(
      cmp, dyn_cast<Instruction>(cmp)->getNextNode(), false);

  BasicBlock *BBThen = br->getParent();
  BasicBlock *BBPrev = BBThen->getSinglePredecessor();
  BasicBlock *BBEnd = BBThen->getSingleSuccessor();

  store->moveBefore(br);

  llvm::SmallVector<Instruction *, 10> marked =
    mark_instructions_to_be_moved(store);

  for_each(marked, [](Instruction *inst) {
    DEBUG(dbgs() << " Marked: " << *inst << "\n");
  });

  move_marked_to_basic_block(marked, br);

  move_from_prev_to_then(BBPrev, BBThen);
  // move_from_prev_to_end(BBPrev, BBThen, BBEnd);
}

// This should implement a cost model
// Right now we only insert the `if` if the depth is >= threshold(1)
// TO-DO: Use a more sophisticated solution
bool DAG::worth_insert_if(Geps &g) {
  if (g.get_loop_depth() >= threshold)
    return true;

  DEBUG(dbgs() << "skipping: " << *g.get_instruction() << "\n"
    << " threshold " << g.get_loop_depth() << " is not greater than " << threshold << "\n\n");
  return false;
}

bool DAG::filter_instructions(Geps &g){
  Instruction *I = g.get_instruction();
  
  switch(I->getOpcode()){
    case Instruction::Add:
    case Instruction::FAdd:
    case Instruction::Mul:
    case Instruction::FMul:
    case Instruction::Xor:
    case Instruction::And:
    case Instruction::Or:
    case Instruction::Sub:
    case Instruction::FSub:
    case Instruction::Shl:
    case Instruction::LShr:
    case Instruction::AShr:
    case Instruction::UDiv:
    case Instruction::SDiv:
      return true;
    default:
      return false;
  }
}

bool DAG::runOnFunction(Function &F) {

  if (F.isDeclaration() || F.isIntrinsic() || F.hasAvailableExternallyLinkage())
    return true;

  Identify *Idn = &getAnalysis<Identify>();

  llvm::SmallVector<Geps, 10> gs = Idn->get_instructions_of_interest();

  if (F.getName() != "cftfsub")
    return false;

  if (gs.size() > 0)
    errs() << "-> #Instructions of interest (" << F.getName() << "): " << gs.size() << "\n";

  // Let's give an id for each instruction of interest
  for (auto &g : gs) {
    Instruction *I = g.get_instruction();

    // sanity check for vector instructions
    if (I->getOperand(0)->getType()->isVectorTy() ||
        I->getOperand(1)->getType()->isVectorTy()){
      continue;
      // errs() << *I << "\n";
      // assert(0 && "Vector type");
    }

    phoenix::StoreNode *store = cast<phoenix::StoreNode>(myParser(g.get_store_inst()));

    ConstraintVisitor cv(store, &g);
    DepthVisitor dv(store);
    DotVisitor t(store);
    // t.print();

    std::set<phoenix::Node*, NodeCompare> *s = dv.getSet();
    if (s->size() > 0){
      errs() << "#Set (" << F.getName() << "): " << s->size() << "\n";
    }

    for (auto node : *s){
      // filter_instructions => Filter arithmetic instructions
      // worth_insert_if     => Cost model

      if (filter_instructions(g) && worth_insert_if(g)){
        errs() << "Trying: " << *node << "\n";
        insert_if(g, node);
      }
      else {
        errs() << "Skipping: " << *node << "\n";
      }
    }

    if (s->size() > 0)
      errs() << "\n";

  }
  
  if (gs.size() > 0)
    errs() << "<- \n";

  return false;
}

void DAG::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<Identify>();
  AU.addRequired<LoopInfoWrapperPass>();
  AU.setPreservesAll();
}

char DAG::ID = 0;
static RegisterPass<DAG> X("DAG", "DAG pattern a = a OP b", false, false);

