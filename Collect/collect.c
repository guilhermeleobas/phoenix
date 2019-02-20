#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "collect.h"

void record_load(long long id, void *address) {
  // printf("Record load with ID: %lld -> %p\n", id, address);
  records[id] = address;
}

void record_store(long long store_id, void *address) {
  // printf("Record store(%d) %p\n", store_id, address);

  int i = 0;
  while (dependency[store_id][i] != -1) {
    int load_id = dependency[store_id][i];

    // printf("\tload(%d): %p\n", load_id, records[load_id]);

    if (records[load_id] == address) {
      store_after_load++;
      return;
    }

    i++;
  }
}

void count_store() { num_dynamic_stores++; }

void init_instrumentation(unsigned num_static_stores,
                          unsigned num_static_loads) {

  // printf("#stores = %d\n", num_static_stores);
  // printf("#loads = %d\n", num_static_loads);

  records = (void *)malloc(sizeof(void *) * num_static_loads);
  for (int i = 0; i < num_static_loads; i++)
    records[i] = NULL;

  dependency = (int **)malloc(sizeof(int *) * num_static_stores);
  for (int i = 0; i < num_static_stores; i++) {
    dependency[i] = malloc(sizeof(int) * num_static_loads);
    dependency[i][0] = -1;
  }

  FILE *f = fopen("map.txt", "r");
  int store_id, cnt;

  while (fscanf(f, "%d %d", &store_id, &cnt) != EOF) {
    // printf("store(%d) -> ", store_id);
    int load_id;
    for (int i = 0; i < cnt; i++) {
      fscanf(f, "%d", &load_id);
      // printf("load(%d) ", load_id);
      dependency[store_id][i] = load_id;
      dependency[store_id][i + 1] =
          -1; // mark always the next pos with a sentinel value
    }
    // printf("\n");
  }
}

void dump_txt() {
  FILE *f = fopen(FILENAME, "w");
  fprintf(f, "%lld, %lld\n", store_after_load, num_dynamic_stores);
  printf("%lld, %lld\n", store_after_load, num_dynamic_stores);
}

/////////////////////////

char* TYPE(opcode){
  for (unsigned i = 0 ; i < LENGTH ; i++){
    if (data[i].opcode == opcode){
      return data[i].name;
    }
  }
  assert(0);
}

dynamic_execution* resize(dynamic_execution *dyn, int curr_size, int new_size) {
  dyn = (dynamic_execution *)realloc(dyn, sizeof(dynamic_execution) * new_size);

  for (int i = curr_size; i < new_size; i++) {
    dyn[i].cnt_id = 0;
    dyn[i].cnt_wi = 0;
    dyn[i].a = 0;
    dyn[i].b = 0;
    dyn[i].both = 0;
  }

  return dyn;
}

void record_id_individually(arithmetic_inst *ai, long long static_id, int has) {
  // realloc if needed
  if (ai->size <= static_id) {
    ai->dyn = resize(ai->dyn, ai->size, static_id + 1);
    // set the correct size
    ai->size = static_id + 1;
  }

  if (has == BOTH){
    ai->dyn[static_id].cnt_id++;
    ai->dyn[static_id].both++;
  }
  else if (has == ID_A){
    ai->dyn[static_id].cnt_id++;
    ai->dyn[static_id].a++;
  }
  else if (has == ID_B){
    ai->dyn[static_id].cnt_id++;
    ai->dyn[static_id].b++;
  }
  else{
    ai->dyn[static_id].cnt_wi++;
  }

}

// Returns 0 if neither a or b are the identity
// 1 if a is the identity
// 2 if b is the identity
// 3 if both a and b are the identity;
//
// @opcode is self-explanatory
// @a and @b are the actual values involved in the arithmetic instruction
// @op_pos tells whether @a or @b is the destiny operand. For instance, since
// we only keep track of instructions of the form `*p += v`, *p is either @a or @b
//  *p = *p + v = a + b
//     = *p + b
//     = a + *p
// We only consider that there is an identity iff the operand holding the identity
// value is not the one indicated by op_pos. 
//
// This function returns which operand holds the identity
//
int has_identity(unsigned opcode, void* a, void* b, unsigned op_pos) {
  switch (opcode) {
  case 13: // Sub
    if (op_pos == FIRST && LL(b) == 0) // *p = *p - 0
      return ID_B;
    return NONE;
  case 11: // Add
  case 28: // Xor
    if (op_pos == SECOND && LL(a) == 0) // *p = 0 + *p (...)
      return ID_A;
    else if (LL(b) == 0) // *p = *p + 0 (...)
      return ID_B;
    else
      return NONE;

  case 15: // Mul
  case 17: // UDiv
  case 18: // SDiv
    if (op_pos == SECOND && LL(a) == 1) // *p = 1 x *p
      return ID_A;
    else if (op_pos == FIRST && LL(b) == 1) // *p = *p x 1
      return ID_B;
    else
      return NONE;

  case 26: // And
  case 27: // Or
    return (LL(a) == LL(b)) ? BOTH : NONE;

  case 12: //Fadd
    if (op_pos == SECOND && DB(a) == 0.0) // *p = 0.0 + *p
      return ID_A;
    else if (op_pos == FIRST && DB(b) == 0.0) // *p = *p + 0.0
      return ID_B;
    return NONE;

  case 14: // FSub
    if (op_pos == FIRST && DB(b) == 0.0) // *p = *p - 0.0
      return ID_B;
    return NONE;

  case 16: // FMul
    if (op_pos == SECOND && DB(a) == 1.0) // *p = 1.0 x *p
      return ID_A;
    else if (op_pos == FIRST && DB(b) == 1.0) // *p = *p x 1.0
      return ID_B;
    else
      return NONE;

  case 19: // FDiv
    if (op_pos == FIRST && DB(b) == 1.0) // *p = *p / 1.0
      return ID_B;
    else
      return NONE;

  case 23: // Shl
  case 24: // LShr
  case 25: // AShr
    if (op_pos == FIRST && LL(a) == 0) // *p = *p << 0
      return ID_B;
    return NONE;

  default:
    assertf(0, "Opcode: %s", TYPE(opcode));
  }
}

unsigned get_index(unsigned opcode){
  unsigned index = -1;
  
  for (unsigned i = 0; i < LENGTH; i++)
    if (data[i].opcode == opcode) {
      index = i;
      break;
    }

  assertf(index >= 0 && index <= LENGTH, "opcode = %s - index = %d", TYPE(opcode), index);

  return index;
}

// Given an arithmeic instruction of the type:
//   c = c `op` b   or c = a `op` c
// record if the variables `a` or `b` are the identity value
// for the arithmetic instruction given by `op`.
// 
// The question remaining is now to detect which operand is `c` (a or b)
// To that end, we also record the address of `c` as well as the address of
// the operand that is supposed to be `c`. This operand was detected by a static
// analysis and may not be precise. We are sending it to check it with an assert!
// 
// So, `dest_address` and `op_address` are those values described below. The unsigned
// value `op_pos` tells us which operand our static analysis detected as being `c`.
// This value is either 1 (FIRST) or 2 (SECOND)
//
void record_arith(unsigned opcode, long long static_id, void* a,
                  void* b, void *dest_address, void *op_address, unsigned op_pos) {

  assert(op_pos == FIRST || op_pos == SECOND);
  assertf(dest_address == op_address, "[%s(%lld)]: %p - %p", TYPE(opcode), static_id, dest_address, op_address);

  unsigned index = get_index(opcode);

  data[index].total_exec++;

  int has = has_identity(opcode, a, b, op_pos);
  if (has)
    data[index].identity_exec++;

  record_id_individually(&data[index], static_id, has);

  /* printf("opcode: %d with value: %lld\n", opcode, value); */
}

void record_arith_int(unsigned opcode, long long static_id, long long a,
                      long long b, void *dest_address, void *op_address,
                      unsigned op_pos) {
  record_arith(opcode, static_id, (void*)&a, (void*)&b, dest_address, op_address, op_pos);
}

void record_arith_float(unsigned opcode, long long static_id, double a,
                        double b, void *dest_address, void *op_address, unsigned op_pos) {
  record_arith(opcode, static_id, (void*)&a, (void*)&b, dest_address, op_address, op_pos);
}

char* get_filename(char *name){
  char *fname = (char*) malloc(sizeof(char) * (5 + strnlen(name, 4)));
  sprintf(fname, "%s.txt", name);
  return fname;
}

void dump_by_type(arithmetic_inst *ai) {
  FILE *f = fopen(get_filename(ai->name), "w");

  long long total = 0;

  for (int i = 0; i < ai->size; i++) {
    dynamic_execution de = ai->dyn[i];
    total += de.cnt_id + de.cnt_wi;
    if (de.cnt_id >= de.cnt_wi){
      fprintf(f, "*static_id[%d] = %lld(%lld, %lld, %lld), %lld\n", i, de.cnt_id, de.a,
              de.b, de.both, de.cnt_wi);
    }
    else {
      fprintf(f, "static_id[%d] = %lld(%lld, %lld), %lld\n", i, de.cnt_id, de.a,
              de.b, de.cnt_wi);
    }
  }

  assert(total == ai->total_exec);

  fclose(f);
}

void dump_arith() {
  FILE *f = fopen("arith.txt", "w");

  for (int i = 0; i < LENGTH; i++) {
    dump_by_type(&data[i]);
    fprintf(f, "%s,%llu,%llu\n", data[i].name, data[i].identity_exec,
            data[i].total_exec);
  }
  fprintf(f, "\n");

  fclose(f);
}
