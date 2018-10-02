#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "collect.h"

void record_load(long long id, void *address){
  printf("Record load with ID: %d -> %p\n", id, address);
  records[id] = address;
}

void record_store(long long id, void *address){
  printf("Record store with ID: %d -> %p\n", id, address);
  if (records[id] == address)
    counter++;
}

void init(){
  for (int i=0; i<MAX; i++)
    records[i] = NULL;
}

void dump(){
  printf("eq: %d", counter);
}
