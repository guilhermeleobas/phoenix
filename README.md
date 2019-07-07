# Phoenix

**Disclaimer: This is still a work in progress!**

<!-- ![Arnoldao Sangue Bom](https://media.giphy.com/media/9wv8qIAq9njgY/giphy-downsized.gif) -->
![PLDI2019](https://i.imgur.com/5grNQoc.png)

*The PLDI 2019 logo*

## Goal

The goal of this project is to identify arithmetic instructions on LLVM Intermediate Representation that

1. Accepts an identity (i.e. +, -, \*, /, ...)

2. Loads and writes into the same memory address (\*p = \*p + v)

This README serves me as a form of documentation for future reference.

## Why do we want to attack this problem?

when working on the silent store paper[1], we perceive that we can optimize instructions that accepts an identity. This project implements a set of arithmetic speculative optimization at IR level. For instance, consider the matrix multiplication algorithm:

```
  C[i,j] += A[i,k]*B[k,j]
```

Whenever `A[i,k]` or `B[k,j]` is zero, we don't need to perform the entire computation. We call this type of optimization a ring optimization because the principal operation (the +) admits an identity and the following one (the *) admits an absorbing element which is equals to the identity: identity = absorbing = zero.

[1] Fernando Pereira, ​Guilherme Leobas​​ and Abdoulaye Gamatié. Static Prediction of Silent Stores - ​ACM Transactions on Architecture and Code Optimization​ - July, 2018 (to appear)

## LLVM Passes

### `/Identify`

We first developed a static analysis (`/Identify`) to see how easily we can identify this kind of pattern. Given an arithmetic instruction I:
```
 I: %dest = `op` %a, %b
```
There are 5 conditions that should be met in order to assume that `I` follows the pattern:

1. `I` should be an arithmetic instruction of interest. See `Identify.cpp:is_arith_inst_of_interest(I)`.

2. `%dest` MUST be used in a store:
  ```
    store %dest, %ptr
  ```
  At the moment we only care in optimizing instructions that writes into memory.

3. either `%a` or `%b` must be loaded from the same `%ptr`
  ```
  %a/%b = load %ptr
  ```

4. Both %base and %offset should be the same
  ```
    ptr = getElementPtr %base, %offset
  ```
4. Both instructions must be on the same basic block!
```
    while (x > 0) {
      y = gep p, 0, x
    }
    ...
    z = gep p, 0, x
```
  In the case above, geps are the same but the first one will
  not have the same value all the time! Therefore, it's important
  that we only check for geps that are only on the same basic block!

5. Both geps should be of the same type!
```
     p = global int
     y = gep p, 0, x
     z = gep cast p to char*, 0, x
```
   In the case above, both geps will hold diferent values since the first
   is a gep for an int* and the second for a char*

Idea: Use RangeAnalysis here to check the offset? Maybe!?
If we use RangeAnalysis, we can drop check 4 when the base pointers are the same


### `CountArith`

This is a profiler that I wrote to see if the static analysis is detecting the pattern correctly. For instance, we can see at runtime if one of the operands indeed was loaded from the same memory address that it's being written. We also track how many times the instruction took the identity as one of the operands.

Each program that uses this LLVM pass must be linked with `/Collect/collect.c` because that's where the logic behind the profiler is. In this LLVm pass, we only add calls to the functions defined there.

In summary, for each instruction marked as interesting by our static analysis, we add a call to a function defined in `Collect/collect.c`.

### `PDG`

This pass implements a program dependence analysis finding all data and control dependences for any given instruction in a function. 

### `ProgramSlicing`

This pass implements a program slicing using the **program dependence graph** pass as a start point.


### `DAG`

This is where I keep all the logic to optimize this pattern. There are currently three approachs implemented to optimize this pattern and they will be describe and they all rely on some auxiliar files:

- DAG/node.cpp: Wrapper for a LLVM::Value or LLVM::Instruction into a node in the Tree.
- DAG/parser.cpp: Given a start point (the store instruction), builds the **expression tree** walking backwards in the operands of the store.
- DAG/visitor.h: The abstract interface for the visitor pattern
- DAG/dotVisitor.h: Generates a dot from the Tree to visualize it
- DAG/propagateAnalysisVisitor.h: Walks on the **Tree** and mark every node that when it equals to the identity, "kills" the entire expression
- DAG/depthVisitor.h: Walks the tree capturing the nodes that *hasConstant()* returns true. Note, this has nothing to do with constraint analysis.
- DAG/constantWrapper.h: Just a wrapper for a LLVM::Constant

We currently have three different approaches implemented for optimizing this pattern.
1. **insertIf.cpp**: Implements the most trivial idea: Add a conditional before the store checking if the value that we are writting is already in memory (a silent store check basically). 
2. **insertIf.cpp**: Implements the most trivial idea(2): For every node that hasConstant() returns true, insert a conditional on it
3. **auto_profile.cpp**: The problem with the trivial approach is that our optimization is speculative, thus it depends on the values given to the program at runtime. To overcome this, we clone the basic block and insert code to profile it at runtime. After profiling, one can decide on what instructions worth insert the conditional. 
```
Loop Header --> BB --> Loop Latch --+
   ^--------------------------------|
```

After optimization: 
```
Loop Header --> BB --> Loop Latch  --+
   ^        +-> BBOpt --^    ^       |
   ^        +-> BBProfile ---^       |
   ^---------------------------------|
```

To summarize: the idea is that we keep the original basic block (the one with the arithmetic expression), a copy of it in which we optimized it (BBOpt) and a third one which we profile the instructions for a few iterations. After those iterations, one can decide if it is best to always execute the original basic block (BB) or the optimzed one (BBOpt). 

4. **manual_profile.cpp**: The problem of the auto_profile.cpp is that we do profilling in the same loop that the original basic block is and this can prevent vectorization from happening. The ideia is to profile the basic block outside the loop. I am still implement this idea but involves performing a program slice in the loop to a function and keep only the necessary instructions to profile a specific array/matrix.

## Benchmarks

We have a [collection of more than 200 benchmarks](https://github.com/guilhermeleobas/Benchmarks) in another repo. We also have developed a [simple framework](https://github.com/guilhermeleobas/tf) written in bash that one can easily compile, instrument, profile, execute those benchmarks.

We have expressive gains on PolyBench. On **cholesky.c** for instance, the speedup is about 80% when compared to the same benchmark compiled with -O3.
