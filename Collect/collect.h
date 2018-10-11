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

