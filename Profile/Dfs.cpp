#include "llvm/ADT/Statistic.h" // For the STATISTIC macro.
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/IR/Constants.h"         // For ConstantData, for instance.
#include "llvm/IR/DebugInfoMetadata.h" // For DILocation
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h" // To use the iterator instructions(f)
#include "llvm/IR/Instructions.h" // To have access to the Instructions.
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"       // To print error messages.
#include "llvm/Support/raw_ostream.h" // For dbgs()

#include "Dfs.h"

#include <stack>
#include <vector>

using std::stack;
using std::vector;

#define DEBUG_TYPE "Dfs"

unsigned Dfs::get_id(Instruction *I) {
  if (inst_to_id.find(I) == inst_to_id.end()) {
    inst_to_id[I] = inst_to_id.size();
    id_to_inst[inst_to_id[I]] = I;
  }

  return inst_to_id[I];
}

Instruction *Dfs::get_instruction(unsigned id) { return id_to_inst[id]; }

unsigned Dfs::get_num_instructions(Function &F) {

  unsigned num_instructions = 0;

  for (auto &BB : F) {
    for (auto &I : BB) {
      num_instructions++;
      get_id(&I);
    }
  }

  return num_instructions;
}

Graph Dfs::build_graph(Function &F) {

  Graph G(get_num_instructions(F));

  for (auto &BB : F) {
    for (auto &I : BB) {
      unsigned u = get_id(&I);
      for (auto op : I.users()) {
        if (Instruction *other = dyn_cast<Instruction>(op)) {
          unsigned v = get_id(other);
          G[u].push_back(v);
        }
      }
    }
  }

  return G;
}

void Dfs::print_graph(const Graph &G) {
  for (int i = 0; i < G.size(); i++) {
    errs() << "[" << i << "]: " << *get_instruction(i) << "\n";
    for (int j = 0; j < G[i].size(); j++) {
      errs() << "\t[" << G[i][j] << "]: " << *get_instruction(G[i][j]) << "\n";
    }
    errs() << '\n';
  }
}

vector<Instruction *>
Dfs::get_instructions_in_path(const vector<unsigned> &parent, unsigned dest) {
  vector<Instruction *> insts;

  int i = dest;

  insts.push_back(get_instruction(i));
  while (parent[i] != i) {
    i = parent[i];
    insts.push_back(get_instruction(i));
  }

  std::reverse(insts.begin(), insts.end());

  return insts;
}

void Dfs::get_all_paths(const Graph &G, vector<unsigned> &visited,
                        vector<unsigned> &parent, unsigned source,
                        unsigned dest) {

  int u = source;

  if (u == dest) {
    vector<Instruction*> insts = get_instructions_in_path(parent, dest);
    Flowpath fp(insts);
    fps.push_back(fp);
    return;
  }

  visited[u] = 1;
  for (auto &v : G[u]) {
    if (visited[v])
      continue;

    parent[v] = u;

    get_all_paths(G, visited, parent, v, dest);

    parent[v] = v;
  }
  visited[u] = 0;
}

vector<Flowpath> Dfs::get_all_paths(Instruction *source, Instruction *dest) {

  vector<unsigned> visited(num_instructions, 0);

  vector<unsigned> parent(num_instructions);
  for (int i = 0; i < parent.size(); i++)
    parent[i] = i;

  unsigned source_id = get_id(source), dest_id = get_id(dest);

  get_all_paths(G, visited, parent, source_id, dest_id);

  return fps;
}

bool Dfs::runOnFunction(Function &F) {
  G.clear();
  fps.clear();
  inst_to_id.clear();
  id_to_inst.clear();
  num_instructions = 0;

  num_instructions = get_num_instructions(F);
  G = build_graph(F);

  return false;
}

char Dfs::ID = 0;
static RegisterPass<Dfs> Z("Dfs", "Find all paths between two instructions");