// The goal of this profiler is to detect identity in arithmetic
// instructions of the type:
//   *p = *p `op` v
// where *p is a memory position and `op` is an arithmetic instruction
// that supports identity. For instance, if `op` is add, we want to find
// when `v` holds the value 0 (zero) and *p appears on both sides of the instruction
//
// We insert call to functions defined here using LLVM instrumentation
// ... see /CountArith/Count.cpp ...
// This library is then compiled and optimized with the final benchmark binary
//

#pragma once

// This file includes an Enum saying which operand is the target operand
#include "../Identify/Position.h" 

#define FILENAME "store_count.txt"
// #define MAX 100000

//
// `records` record each memory access (load/store)
// 

// static void* records[MAX];
static void **records;
static long long store_after_load = 0;
static long long num_dynamic_stores = 0;

//  
//  mapping between each store and its dependencies
//  store(0) -> load(0), load(2), load(5), load(xyz)
//  store(1) -> load(1)
//  store(2) -> []
//  store(3) -> load(0), load(1), ...
//  ...
//
static int **dependency;

void record_load(long long, void *);
// void record_store(long long, void *);
void count_store();

void init_instrumentation(unsigned total_static_stores,
                          unsigned total_static_loads);
void dump_txt();

//

enum ID_TYPES { NONE = 0, ID_A = 1, ID_B = 2, BOTH = 3 };

#define max(a, b)                                                              \
  ({                                                                           \
    __typeof__(a) _a = (a);                                                    \
    __typeof__(b) _b = (b);                                                    \
    _a > _b ? _a : _b;                                                         \
  })

#define LL(a) (*(long long *)(a))

#define DB(a) (*(double *)(a))

#define assertf(A, M, ...) if(!(A)) \
 { fprintf(stderr, M, ##__VA_ARGS__); fprintf(stderr, "\n"); assert(A); }


typedef struct {
  long long cnt_id;     // The number of times this instruction was executed with an
                        // identity
  long long cnt_wi;     // The number of times this instruction was executed without
                        // an identity

  long long a, b, both; // The number of times the operand a or b are the
                        // identity Note that cnt_id = a + b;
} dynamic_execution;

typedef struct {
  char *name;
  unsigned opcode;
  unsigned long long identity_exec;
  unsigned long long total_exec;

  //
  unsigned size;
  dynamic_execution *dyn;
} arithmetic_inst;

#define LENGTH 15

static arithmetic_inst data[LENGTH] = {
    {"Add", 11, 0, 0, 0, NULL},  {"FAdd", 12, 0, 0, 0, NULL}, {"Sub", 13, 0, 0, 0, NULL},
    {"FSub", 14, 0, 0, 0, NULL}, {"Mul", 15, 0, 0, 0, NULL},  {"FMul", 16, 0, 0, 0, NULL},
    {"Xor", 28, 0, 0, 0, NULL},  {"Shl", 23, 0, 0, 0, NULL},  {"LShr", 24, 0, 0, 0, NULL},
    {"AShr", 25, 0, 0, 0, NULL}, {"UDiv", 17, 0, 0, 0, NULL}, {"SDiv", 18, 0, 0, 0, NULL},
    {"And", 26, 0, 0, 0, NULL},  {"Or", 27, 0, 0, 0, NULL}, {"FDiv", 19, 0, 0, 0, NULL},
};

int has_identity(unsigned opcode, void *a, void *b, unsigned op_pos);
dynamic_execution *resize(dynamic_execution *dyn, int curr_size, int new_size);
void record_id_individually(arithmetic_inst *ai, long long static_id,
                            int is_identity);
unsigned get_index(unsigned opcode);
void record_arith(unsigned opcode, long long static_id, void *a, void *b,
                  void *dest_address, void *op_address, unsigned op_pos);
void record_arith_int(unsigned opcode, long long static_id, long long a,
                      long long b, void *dest_address, void *op_address,
                      unsigned op_pos);
void record_arith_float(unsigned opcode, long long static_id, double a,
                        double b, void *dest_address, void *op_address,
                        unsigned op_pos);


//

typedef struct {
  unsigned store_id;
  unsigned is_marked;
  unsigned long long silent;
  unsigned long long total;
} record;

unsigned size = 0;

record *r = NULL;

void realloc_records(unsigned new_size);
void record_store(unsigned store_id, unsigned is_marked, unsigned is_equals);
void dump_records();



