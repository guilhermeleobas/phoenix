#pragma once

#define FILENAME "count.csv"
#define MAX 10000

static char* records[MAX];
static int counter = 0;

void record_load(long long, void*);
void record_store(long long, void*);

void init();
void dump();

