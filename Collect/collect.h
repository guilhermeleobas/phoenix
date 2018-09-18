#pragma once

#define FILENAME "count.csv"

typedef struct Instruction{
  char name[10];
  unsigned long long counter;
} Instruction;

static Instruction array[10000];
static int size = 0;

void count_instruction(char*);
void dump_csv();

void dump_inst(char*);


long long int add_inc = 0;
long long int fadd_inc = 0;

long long int sub_inc = 0;
long long int fsub_inc = 0;

long long int mul_inc = 0;
long long int fmul_inc = 0;

long long int udiv_inc = 0;
long long int sdiv_inc = 0;
long long int fdiv_inc = 0;

long long int urem_inc = 0;
long long int srem_inc = 0;
long long int frem_inc = 0;

long long int shl_inc = 0;
long long int lshr_inc = 0;
long long int ashr_inc = 0;

long long int and_inc = 0;
long long int or_inc = 0;
long long int xor_inc = 0;
