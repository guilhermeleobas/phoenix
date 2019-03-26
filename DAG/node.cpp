#include "node.h"
#include <algorithm>

// int phoenix::Counter::ID = 0;

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