#pragma once

using namespace llvm;

using std::vector;
using std::map;

#include "Graph.h"
#include "Flowpath.h"

class Dfs : public FunctionPass {
public: 
  // Pass identifier, for LLVM's RTTI support:
  static char ID;

  bool runOnFunction(Function&);

  Dfs() : FunctionPass(ID) {}
  ~Dfs() { }


  void get_all_paths(Instruction *source, Instruction *dest);

private:

  // Return the total number of LLVM IR instructions for a given function 
  unsigned get_num_instructions(Function &F);

  // Build a graph using each instruction id as nodes
  Graph build_graph(Function &F);

  // Print the graph
  void print_graph(const Graph &G);
  vector<Instruction*> get_instructions_in_path(const vector<unsigned> &parent, unsigned dest);

  
  //  Map each instruction to a unique ID;
  unsigned get_id(Instruction*);
  Instruction* get_instruction (unsigned id);




  // Find all paths from a source to a destiny in a graph
  void get_all_paths(const Graph &G, vector<unsigned> &visited,
                        vector<unsigned> &parent, unsigned source,
                        unsigned dest);

  // -=-=-=-=-=-=-=-=-=-=-=- VARIABLES -=-=-=-=-=-=-=-=-=-=-=-

  //  Give a unique ID to each instruction
  map<Instruction*, unsigned> inst_to_id;
  map<unsigned, Instruction*> id_to_inst;

  Graph G;
  unsigned num_instructions;
  vector<Flowpath> fps;

};

