
#include "llvm/ADT/Statistic.h" // For the STATISTIC macro.
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/IR/Constants.h"         // For ConstantData, for instance.
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h" // To use the iterator instructions(f)
#include "llvm/IR/Instructions.h" // To have access to the Instructions.
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Metadata.h"
#include "llvm/Support/raw_ostream.h" // For dbgs()
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/IR/Instructions.h" // To have access to the Instructions.

#include "parser.h"

using namespace llvm;

phoenix::Node* myParser(BasicBlock *BB, Value *V){

  if (Constant *C = dyn_cast<Constant>(V)){
    if (isa<ConstantInt>(C))
      return new phoenix::ConstantIntNode(C);
    return new phoenix::ConstantNode(C);
  }
  else if (Instruction *I = dyn_cast<Instruction>(V)){
    if (I->getParent() != BB)
      return new phoenix::ForeignNode(I);

    if (isa<InsertElementInst>(I) ||
        isa<SelectInst>(I) ||
        isa<PHINode>(I) ||
        isa<GetElementPtrInst>(I) ||
        isa<CallInst>(I))
      return new phoenix::TerminalNode(I);

    if (isa<LoadInst>(I))
      return new phoenix::LoadNode(I);

    if (isa<BinaryOperator>(I) ||
        isa<CmpInst>(I)){
      phoenix::Node *left = myParser(BB, I->getOperand(0));
      phoenix::Node *right = myParser(BB, I->getOperand(1));
      return new phoenix::BinaryNode(left, right, I);
    }
    else if (isa<CastInst>(I)){
      phoenix::Node *node = myParser(BB, I->getOperand(0));
      return new phoenix::CastNode(node, I);
    }
    else if (isa<UnaryInstruction>(I)){
      phoenix::Node* node = myParser(BB, I->getOperand(0));
      return new phoenix::UnaryNode(node, I);
    }
    else if (StoreInst *store = dyn_cast<StoreInst>(I)){
      phoenix::Node *node = myParser(BB, store->getValueOperand());
      if (phoenix::BinaryNode *binary = dyn_cast<phoenix::BinaryNode>(node)){
        phoenix::TargetOpNode *top = new phoenix::TargetOpNode(binary);
        return new phoenix::StoreNode(top, store);
      }
      else {
        assert (0 && "store child must be a binary node!");
      }
    }
  }
  else if (isa<Argument>(V)){
    return new phoenix::ArgumentNode(V);
  }

  std::string str = "Instruction not supported (parsing): ";
  llvm::raw_string_ostream rso(str);
  V->print(rso);
  BB->print(rso);
  llvm_unreachable(str.c_str());

  return nullptr;
}

phoenix::Node* myParser(Instruction *I){
  return myParser(I->getParent(), I);
}