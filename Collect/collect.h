#pragma once

#define FILENAME "store_count.txt"
#define MAX 10000

// static std::map<long long, void*> records;
// static std::map<void*, unsigned> cnt;
static void* records[MAX];
static long long counter = 0;
static int max_id = 0;

void record_load(long long, void*);
void record_store(void*);

void init();
void dump();

