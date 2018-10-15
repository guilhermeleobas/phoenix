#pragma once

// This class represents a flow path between source (load) and a destiny (store)
// Each
class Flowpath {

private:
  // -=-=-=-=-=-=-=-=-=-=-=- VARIABLES -=-=-=-=-=-=-=-=-=-=-=-

  std::vector<Instruction *> instructions;
  unsigned types;         // Arithmetic instructions involved in the path
  unsigned inst_distance; // Distance by the number of instructions from
                          // source -> dest
  unsigned BB_distance;   // Distance by the number of basic blocks

public:
  Flowpath(std::vector<Instruction *> instructions)
      : instructions(instructions), inst_distance(0), BB_distance(0), types(0) {
    calc_inst_distance();
    calc_BB_distance();
    set_arithmetic_types();
  }

  unsigned get_inst_instance() const { return this->inst_distance; }

  unsigned get_BB_distance() const { return this->BB_distance; }

  // Check if the path contains an arithmetic instruction with the @opcode
  bool is_opcode_set(unsigned opcode) const {
    return (this->types >> opcode) & 1U;
  }

private:
  // Calc the number of instructions in the path
  void calc_inst_distance() { this->inst_distance = this->instructions.size(); }

  // Calc the number of basic blocks between the source and the destiny
  // This calc is not 100% precise
  void calc_BB_distance() {
    for (int i = 1; i < this->instructions.size(); i++) {
      BasicBlock *prev = this->instructions[i - 1]->getParent();
      BasicBlock *prox = this->instructions[i]->getParent();

      if (prev != prox)
        this->BB_distance++;
    }
  }

  // Set a bit corresponding to each arithmetic opcode in the flowpath
  // For instance, one set the 11˚ bit if the instruction being analyzed
  // is an ADD. Similarly, one set the 28˚ bit if the instruction is a XOR
  void set_arithmetic_types() {
    for (const Instruction *I : this->instructions) {
      if (I->isBinaryOp()) {
        switch (I->getOpcode()) {
        case Instruction::Add:
        case Instruction::FAdd:
        //
        case Instruction::Sub:
        case Instruction::FSub:
        //
        case Instruction::Mul:
        case Instruction::FMul:
        //
        case Instruction::UDiv:
        case Instruction::SDiv:
        case Instruction::FDiv:
        //
        case Instruction::URem:
        case Instruction::SRem:
        case Instruction::FRem:
        //
        case Instruction::Shl:
        case Instruction::LShr:
        case Instruction::AShr:
        //
        case Instruction::And:
        case Instruction::Or:
        case Instruction::Xor:
          types |= (1UL << I->getOpcode());
        default:
          continue;
        }
      }
    }
  }
};