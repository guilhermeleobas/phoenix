#pragma once

#define FILENAME "store_count.txt"
// #define MAX 100000

/*
  `records` record each memory access (load/store)
*/
// static void* records[MAX];
static void** records;
static long long store_after_load = 0;
static long long num_dynamic_stores = 0;

/*
  mapping between each store and its dependencies
  store(0) -> load(0), load(2), load(5), load(xyz)
  store(1) -> load(1)
  store(2) -> []
  store(3) -> load(0), load(1), ...
  ...
*/
static int** dependency;

void record_load(long long, void*);
void record_store(long long, void*);
void count_store();

void init_instrumentation(unsigned total_static_stores, unsigned total_static_loads);
void dump_txt();

//

typedef struct {
  char *name;
  unsigned opcode;
  unsigned long long identity;
  unsigned long long cnt;
} arithmetic_inst;

#define LENGTH 11

static arithmetic_inst data[LENGTH] = {
  {"Add", 11, 0, 0},
  {"Sub", 13, 0, 0},
  {"Xor", 28, 0, 0},
  {"Shl", 23, 0, 0},
  {"LShr", 24, 0, 0},
  {"AShr", 25, 0, 0},
  {"Mul", 15, 0, 0},
  {"UDiv", 17, 0, 0},
  {"SDiv", 18, 0, 0},
  {"And", 26, 0, 0},
  {"Or", 27, 0, 0},
};

void init_arith();
int has_identity(unsigned opcode, long long a, long long b);
void record_arith(unsigned opcode, long long a, long long b);

