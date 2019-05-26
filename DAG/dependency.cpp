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
#include <vector>

#include "dependency.h"

using namespace llvm;

#define CONTAINS(container, value)                                             \
  (std::find(container.begin(), container.end(), value) != container.end())

namespace phoenix {

Dependency::Dependency(Function *F, PostDominatorTree *PDT): F(F), PDT(PDT) {
  auto v = getNonPostDomEdges();
  updateControlDependencies(v);
}

std::vector<Dependency::Edge> Dependency::getNonPostDomEdges(){
  std::vector<Dependency::Edge> v;

  for (auto &A : *F){
    errs() << "start: " << A.getName() << "\n";
    for (BasicBlock *B : successors(&A)){
      errs() << " - " << B->getName() << "\n";
      if (!PDT->properlyDominates(B, &A)){
        v.push_back(Edge(B, &A));
      }
    }
  }

  return v;
}

void Dependency::updateControlDependencies(const std::vector<Dependency::Edge> &v){

  for (auto &edge : v){
    BasicBlock *A = edge.tail;   
    BasicBlock *B = edge.head;
    BasicBlock *LCA = this->PDT->findNearestCommonDominator(A, B);

    if (LCA == nullptr)
      continue;

    // LCA can be either 1. equals to A or 2. a parent of A
    // 
    // If LCA == A, then all the nodes in the post dominator tree
    // path from B to A (inclusive), should be made control dependent
    // on A
    //
    // If LCA != A, then all nodes from LCA to B (except the LCA) should
    // be made control dependent on A

    auto curr = this->PDT->getNode(B);
    auto parentA = this->PDT->getNode(A)->getIDom();

    while (curr != parentA){
      errs() << "adding " << curr->getBlock()->getName() << " to the set "
             << parentA->getBlock()->getName() << "\n";
      curr = curr->getIDom();
    }
  }
}

std::vector<BasicBlock*> get_control_dependency(){
  std::vector<BasicBlock*> v;

  // for (auto *A : L->getBlocks()){
  //   for (auto *B : successors(A)) {

  //     if (!PDT->properlyDominates(B, A)) {
  //       errs() << "[Control Dependency]: " << B->getName() << " -> "
  //              << A->getName() << "\n";
  //     }
  //   }
  // }

  return v;
}

} // end namespace phoenix
