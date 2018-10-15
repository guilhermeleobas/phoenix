#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Debug.h"        // To print error messages.
#include "llvm/ADT/Statistic.h"        // For the STATISTIC macro.
#include "llvm/IR/InstIterator.h"      // To use the iterator instructions(f)
#include "llvm/IR/Instructions.h"      // To have access to the Instructions.
#include "llvm/IR/Constants.h"         // For ConstantData, for instance.
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Support/raw_ostream.h"  // For dbgs()
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/IR/DebugInfoMetadata.h" // For DILocation
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/IRBuilder.h"

#include "Dfs.h"

#include <stack>
#include <vector>

using std::stack;
using std::vector;

#define DEBUG_TYPE "Dfs"

unsigned Dfs::get_id(Instruction *I){
  if (inst_to_id.find(I) == inst_to_id.end()){
    unsigned id = inst_to_id.size();
    inst_to_id[I] = id;
    id_to_inst[id] = I;
  }

  return inst_to_id[I];
}

Instruction* Dfs::get_instruction(unsigned id){
  return id_to_inst[id];
}

unsigned Dfs::get_num_instructions(Function &F){
  
  unsigned num_instructions = 0;

  for (auto &BB : F){
    num_instructions += std::distance(BB.begin(), BB.end());
    for (auto &I : BB){
      get_id(&I);
    }
  }

  return num_instructions;
}

Graph Dfs::build_graph(Function &F){

  Graph G(get_num_instructions(F));

  for (auto &BB : F){
    for (auto &I : BB){
      unsigned u = get_id(&I);
      for (auto op : I.users()){
        if (Instruction *other = dyn_cast<Instruction>(op)){
          unsigned v = get_id(other);
          Instruction *aux = get_instruction(v);
          G[u].push_back (v);
        }
      }
    }
  }

  return G;
}

void Dfs::print_graph(const Graph &G){
  for (int i=0; i<G.size(); i++){
    errs() << "[" << i << "]: " << *get_instruction(i) << "\n";
    for (int j=0; j<G[i].size(); j++){
      errs() << "\t[" << G[i][j] << "]: " << *get_instruction(G[i][j]) << "\n";
    }
    printf("\n");
  }
}

vector<Instruction*> Dfs::get_instructions_in_path(const vector<unsigned> &parent, unsigned dest){
  vector<Instruction*> insts;

  int i = dest;

  insts.push_back ( get_instruction(i) );
  while(parent[i] != i){
    i = parent[i];
    insts.push_back ( get_instruction(i) );
  }

  std::reverse(begin(insts), end(insts));

  return insts;
}

void Dfs::get_all_paths(const Graph &G, vector<unsigned> &visited,
                        vector<unsigned> &parent, unsigned source,
                        unsigned dest) {

  int u = source;

  // printf(" Node: %d\n", source);
  
  if (u == dest){
    // printf("found a path\n");
    vector<Instruction*> insts = get_instructions_in_path(parent, dest);
    Flowpath fp (insts);
    fps.push_back (fp);
    return;
  }

  visited[u] = 1;
  for (auto &v : G[u]){
    if (visited[v])
      continue;

    parent[v] = u;

    get_all_paths(G, visited, parent, v, dest);

    parent[v] = v;
  }
  visited[u] = 0;
}

void Dfs::get_all_paths(Instruction *source, Instruction *dest){

  vector<unsigned> visited (num_instructions, 0);

  vector<unsigned> parent(num_instructions);
  for (int i=0; i<parent.size(); i++)
    parent[i] = i;

  unsigned source_id = get_id(source), dest_id = get_id(dest);

  printf("From %d to %d\n", source_id, dest_id);
  errs() << *get_instruction(source_id) << " -> " << *get_instruction(dest_id) << "\n";

  get_all_paths(G, visited, parent, source_id, dest_id);
  errs() << '\n';
}

bool Dfs::runOnFunction(Function &F) {


  num_instructions = get_num_instructions(F);
  G = build_graph(F);

  return false;
}

char Dfs::ID = 0;
static RegisterPass<Dfs> Z("Dfs",
    "Find all paths between two instructions");