#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "collect.h"

#define max(a, b) ((a > b) ? (a) : (b))

void record_load(long long id, void *address){
  /* printf("Record load with ID: %lld -> %p\n", id, address); */
  records[id] = address;
  max_id = max(max_id, id);
}

void record_store(void *address){
  /* printf("Record store %p\n", address); */
  
  /* printf("max_id: %d\n", max_id); */
  
  for (int i=0; i<=max_id; i++){
    if (records[i] == address){ 
      counter++;
      return;
    }
  }
  
}

void init_instrumentation(){
  for (int i=0; i<MAX; i++)
    records[i] = NULL;
}

void dump_txt(){
  FILE *f = fopen(FILENAME, "w");
  fprintf(f, "%lld\n", counter);
}
