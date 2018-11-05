# Phoenix

**Disclaimer: This is still a work in progress!**

![Arnoldao Sangue Bom](https://media.giphy.com/media/9wv8qIAq9njgY/giphy-downsized.gif)


## Goal

The goal of this project is to identify arithmetic instructions on LLVM Intermediate Representation that

1. Accepts an identity (i.e. +, -, \*, /, ...)

2. Loads and writes into the same memory address (\*p = \*p + v)

This README serves me as a form of documentation for future reference.

## Why do we want to attack this problem?

While working in the silent stores paper[1], we realize that usually the identity tends to silentness. Our idea is to identify when this pattern happens statically and it with the following code:

```
  if (v != identity_op)
    *p = *p `op` v
```

Our idea is to prevent computation as much as possible. We saw on a few polybench benchmarks that by just inserting this `if` we can get a good speedup

[1] Fernando Pereira, ​Guilherme Leobas​​ and Abdoulaye Gamatié. Static Prediction of Silent Stores - ​ACM Transactions on Architecture and Code Optimization​ - July, 2018 (to appear)

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
    ptr = getElementPtr %base, %offset
  ```
Both %base and %offset should be the same

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

This is a profiler that I wrote to see if the static analysis is indeed detecting this correctly. For instance, we can see at runtime if one of the operands indeed was loaded from the same memory address that it's being written. We also track how many times the instruction took the identity as one of the operands.

Each program that uses this LLVM pass must be linked with `/Collect/collect.c` because that's where the logic behind the profiler is. In this LLVm pass, we only add calls to the functions defined there.

In summary, for each instruction marked as interesting by our static analysis, we add a call to a function defined in `Collect/collect.c`.

## Next steps

Our next step is to write an LLVM pass that will optimize this pattern. But there are still room to improve in the static analysis.

## Benchmarks

We have a [collection of more than 200 benchmarks](https://github.com/guilhermeleobas/Benchmarks) in another repo. We also have developed a [simple framework](https://github.com/guilhermeleobas/tf) written in bash that one can easily compile, instrument, profile, execute those benchmarks.
