#include "node.h"
#include <algorithm>

int phoenix::Counter::ID = 0;

std::string phoenix::get_symbol(Instruction *I) {
  switch (I->getOpcode()) {
    case Instruction::Add:
      return "+";
    case Instruction::FAdd:
      return "+.";
    case Instruction::Sub:
      return "-";
    case Instruction::FSub:
      return "-.";
    case Instruction::Xor:
      return "^";
    case Instruction::Shl:
      return "<<";
    case Instruction::LShr:
    case Instruction::AShr:
      return ">>";
    case Instruction::Mul:
      return "*";
    case Instruction::FMul:
      return "*.";
    case Instruction::UDiv:
    case Instruction::SDiv:
      return "/";
    case Instruction::FDiv:
      return "/.";
    case Instruction::And:
      return "&";
    case Instruction::Or:
      return "|";
    default:
      return I->getOpcodeName();
      // std::string str = "Symbol not found: ";
      // llvm::raw_string_ostream rso(str);
      // I->print(rso);
      // llvm_unreachable(str.c_str());
  }
}

std::string phoenix::getFileName(Instruction *I) {
  MDNode *Var = I->getMetadata("dbg");
  if (Var)
    if (DILocation *DL = dyn_cast<DILocation>(Var))
      return std::string(DL->getFilename());
  return std::string("FILENAME");
}

int phoenix::getLineNo(Value *V) {
  if(!V)
    return -1;
  if (Instruction *I = dyn_cast<Instruction>(V))
      if (MDNode *N = I->getMetadata("dbg"))
        if (DILocation *DL = dyn_cast<DILocation>(N))
          return DL->getLine();
  return -1;
}