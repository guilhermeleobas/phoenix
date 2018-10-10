#pragma once

#define FILENAME "store_count.txt"
#define MAX 100000

static void* records[MAX];
static long long counter = 0;
static long long num_stores = 0;

/*
  mapping between each store and its dependencies
  store(0) -> load(0), load(2), load(5), load(xyz)
  store(1) -> load(1)
  store(2) -> []
  store(3) -> load(0), load(1), ...
  ...
*/
static int dependency[MAX][MAX];

void record_load(long long, void*);
void record_store(long long, void*);
void count_store();

void init_instrumentation();
void dump_txt();

