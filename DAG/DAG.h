#pragma once

using namespace llvm;

#include "../Identify/Geps.h"
#include "../Identify/Identify.h"
#include "node.h"
#include "parser.h"

class DAG : public FunctionPass {
 private:
  unsigned loop_threshold = 1;


  void runDAGOptimization(Function &F, llvm::SmallVector<Geps, 10> &g);

  // Cre
  AllocaInst* create_c1(Function *F, BasicBlock *BBProfile); 
  AllocaInst* create_c2(Function *F, BasicBlock *BBProfile, Value *V, Value *constraint);

  // CloneBasicBlock performs a shallow copy. 
  // This method does a deep copy of BB
  BasicBlock *deep_clone(BasicBlock *BB, ValueToValueMapTy &VMap,
                         const Twine &suffix, Function *F);

  // @F : A pointer to the function @BB lives in
  // @BB : The original basic block 
  // @V : The llvm value that "kills" the expression
  // @constraint: the value that @V must hold to kill the expression
  // 
  // This method creates a copy of @BB, which we called it @BBProfile
  // and insert counters c1 and c2 to:
  //   - @c1: Count the number of times @BBProfile executes.
  //   - @c2: Count the number of times @V == @constraint
  // 
  std::tuple<BasicBlock*, Value*, Value*> create_BBProfile(Function *F, BasicBlock *BB, Value *V,
                                         Value *constraint);

  // @F : A pointer to the function @BB lives in
  // @BB : The original basic block 
  // @store : A pointer to the store that leads to @V
  // @V : The llvm value that "kills" the expression
  // @constraint: the value that @V must hold to kill the expression
  // 
  // Returns a clone of @BB with the conditional. See `insert_if` for more info. 
  BasicBlock *create_BBOpt(Function *F, BasicBlock *BB, StoreInst *store, Value *V, Value *constraint);

  // @F : A pointer to the function @BB lives in
  // @BBProfile : Basic block with counters
  // @switch_control : controls to which target the switch will jump
  //   0 => jumps to BBProfile
  //   1 => jumps to BB
  //   2 => jumps to BBOpt
  // @c1 : # of times @BBProfile were executed
  // @c2 : # of times @V == @constraint on @BBProfile
  // @n_iter : max. number of iterations of @BBProfile
  // @gap : The minimum difference between @c1 and @c2 to use BBOpt instead of BB.
  //   - if @c1 ~ @c2, then use @BB 
  //   - else if @c1 >> @c2, then @V is != @constraint most of the times.
  //   - The @gap controls this difference:
  //       if (@c1 - @c2) > @gap then use @BBOpt
  //       else use @BB
  // 
  // Creates a basic block that changes the value of @switch_control based
  // on the counters. 
  //
  void create_BBControl(Function *F, BasicBlock *BBProfile,
                        Value *switch_control, Value *c1, Value *c2,
                        ConstantInt *n_iter, ConstantInt *gap);
  void create_BBControl(Function *F, BasicBlock *BBProfile, Value *switch_control, Value *c1, Value *c2);

  // @F : A pointer to the function @BB lives in
  // @BB : The original basic block
  // @BBProfile : Basic block with counters
  // @BBOpt : @BB with conditionals
  //
  // Creates a new basic block with a switch conditional managed by the Value
  // @switch_control : controls to which target the switch will jump
  //   0 => jumps to BBProfile (default)
  //   1 => jumps to BB
  //   2 => jumps to BBOpt
  // 
  // returns the instruction that controls the switch jump target
  Instruction *create_switch(Function *F, BasicBlock *BB, BasicBlock *BBProfile,
                             BasicBlock *BBOpt);

  void profile_and_optimize(Function *F, const Geps &g,
                            const phoenix::Node *node,
                            bool justOptimize = true);

 public:
  // Pass identifier, for LLVM's RTTI support:
  static char ID;

  bool runOnFunction(Function &);

  void getAnalysisUsage(AnalysisUsage &AU) const;

  DAG() : FunctionPass(ID) {}
  ~DAG() {}
};
