#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "collect.h"

void count_instruction(char *name){
  for (int i=0; i<size; i++){
    if (strcmp(array[i].name, name) == 0 /* found */){
      ++array[i].counter;
      return;
    }
  }

  strcpy(array[size].name, name);
  ++array[size].counter;
  ++size;
}

void dump_csv(){

  FILE *f;
  f = fopen(FILENAME, "w");
  if (f != NULL){
    
    fprintf(f, "ADD,FADD,SUB,FSUB,MUL,FMUL,UDIV,SDIV,FDIV,UREM,SREM,FREM,SHL,LSHR,ASHR,AND,OR,XOR\n");
    fprintf(f, "%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n", 
              add_inc, fadd_inc,
              sub_inc, fsub_inc,
              mul_inc, fmul_inc,
              udiv_inc, sdiv_inc, fdiv_inc,
              urem_inc, srem_inc, frem_inc,
              shl_inc, lshr_inc, ashr_inc,
              and_inc, or_inc, xor_inc);

    /* if (size){ */
    /*   fprintf(f, "%s", array[0].name); */
    /*   for (int i=1; i<size; i++) */
    /*     fprintf(f, ",%s", array[i].name); */
    /*  */
    /*   fprintf(f, "\n"); */
    /*  */
    /*   fprintf(f, "%llu", array[0].counter); */
    /*   for (int i=1; i<size; i++) */
    /*     fprintf(f, ",%llu", array[i].counter); */
    /*   fprintf(f, "\n"); */
    /*  */
    /* } */
    /* fclose(f); */
  }
  else {
    printf("Cannot create file\n");
  }
}
