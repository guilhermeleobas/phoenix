#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "collect.h"

void record_load(long long id, void *address){
   // printf("Record load with ID: %lld -> %p\n", id, address); 
  records[id] = address;
}

void record_store(long long store_id, void *address){
   // printf("Record store(%d) %p\n", store_id, address);

  int i = 0;
  while (dependency[store_id][i] != -1){
    int load_id = dependency[store_id][i];

    // printf("\tload(%d): %p\n", load_id, records[load_id]);
    
    if (records[load_id] == address){
      store_after_load++;
      return;
    }

    i++;
  }
  
}

void count_store(){
  num_dynamic_stores++;
}

void init_instrumentation(unsigned num_static_stores, unsigned num_static_loads){

  // printf("#stores = %d\n", num_static_stores);
  // printf("#loads = %d\n", num_static_loads);

  records = (void*) malloc(sizeof(void*) * num_static_loads);
  for (int i=0; i<num_static_loads; i++)
    records[i] = NULL;

  dependency = (int**) malloc(sizeof(int*) * num_static_stores);
  for (int i=0; i<num_static_stores; i++){
    dependency[i] = malloc(sizeof(int) * num_static_loads);
    dependency[i][0] = -1;
  }


  FILE *f = fopen("map.txt", "r");
  int store_id, cnt;

  while (fscanf(f, "%d %d", &store_id, &cnt) != EOF){
    // printf("store(%d) -> ", store_id);
    int load_id;
    for (int i=0; i<cnt; i++){
      fscanf(f, "%d", &load_id);
      // printf("load(%d) ", load_id);
      dependency[store_id][i] = load_id;
      dependency[store_id][i+1] = -1; // mark always the next pos with a sentinel value
    }
    // printf("\n");
  }

}

void dump_txt(){
  FILE *f = fopen(FILENAME, "w");
  // printf("equals: %d\n", counter);
  fprintf(f, "%lld, %lld\n", store_after_load, num_dynamic_stores);
  printf("%lld, %lld\n", store_after_load, num_dynamic_stores);
}


///////

int has_identity(unsigned opcode, long long a, long long b){
  switch(opcode){
    case 11: // Add
    case 13: // Sub
    case 28: // Xor
    case 23: // Shl
    case 24: // LShr
    case 25: // AShr
      return (a == 0 || b == 0);
    case 15: // Mul
    case 17: // UDiv
    case 18: // SDiv
      return (a == 1 || b == 1);
    case 26: // And
    case 27: // Or
      return (a == b);
  }
}

// Given an arithmeic instruction of the type:
//   c = a `op` b
// record if the variables `a` or `b` are the identity value
// for the arithmetic instruction given by `op`.
void record_arith(unsigned opcode, long long a, long long b){
  
  int index = -1;
  for (int i=0; i<LENGTH; i++)
    if (data[i].opcode == opcode){
      index = i;
      break;
    }
  assert (index >= 0);

  data[index].cnt++;
  if (has_identity(opcode, a, b))
    data[index].identity++;

  // printf("opcode: %d with value: %lld\n", opcode, value);
}

void dump_arith(){
  FILE *f = fopen("arith.txt", "w");

  for (int i=0; i<LENGTH; i++){
    fprintf(f, "%s, %llu, %llu\n", data[i].name, data[i].identity, data[i].cnt);
    printf("%s, %llu, %llu\n", data[i].name, data[i].identity, data[i].cnt);
  }
  fprintf(f, "\n");

  fclose(f);
  
}
