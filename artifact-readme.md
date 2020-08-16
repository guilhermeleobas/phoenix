# OOPSLA Artifact
#### Paper
* Semiring Optimizations: Dynamic Elision of Expressions with Identity and Absorbing Elements
#### Authors: 
* Guilherme Vieira Leobas
* Fernando Magno Quintao Pereira

# Getting the artifact
```bash
docker pull guilhermeleobas/phoenix:latest
docker run -it guilhermeleobas/phoenix:latest /bin/bash
```

# Building from source
This artifact makes use of some programs available in most major linux
distributions. That said, it was tested only on Ubuntu 16.04 with LLVM 6.0.1.
In case you are also running Ubuntu (other versions may vary), you can set up the artifact by downloading the required programs and setting up the paths in the scripts:

- gcc
- g++
- cmake 
- make
- git
- gnu parallel
- LLVM 6.0.1 (https://releases.llvm.org/download.html)
- phoenix (`git clone github.com:guilhermeleobas/phoenix.git`)
- tf (`git clone --branch phoenix --recursive git@github.com:guilhermeleobas/tf.git`)

After downloading everything, edit `tf/vars.sh` and set `LLVM_PATH` and `PHOENIX_PATH` to the absolute path of LLVM and Phoenix, respectively.

Then, `cd` into `$PHOENIX_PATH` and build the optimization for the first time:
```bash
CMAKE_PREFIX_PATH=$LLVM_PATH \
    CXX=$LLVM_PATH/bin/clang \
    LLVM_DIR=$LLVM_PATH/build/lib/cmake \
    bash rebuild.sh
```

# Directories
This artifact is composed by the following directories:

- **Phoenix**: The optimization implementation;
- **llvm61**: The binaries of LLVM 6.0.1;
- **tf**: A list of scripts that will be used to evaluate the proposed optimization;
- **tf/Benchs**: The 287 benchmarks used in the experiments of this paper;

# TF - Testing Framework
TF contains a set of scripts we use to benchmark the proposed optimization.

## List of Benchmarks
- ASCI_Purple
- ASC_Sequoia
- BenchmarkGame
- BitBench
- CoyoteBench
- Dhrystone
- DOE_ProxyApps_C
- Fhourstones
- Fhourstones_31
- FreeBench
- Linpack
- llubenchmark
- mafft
- MallocBench
- McCat
- McGill
- mediabench
- MiBench
- Misc
- nbench
- NPB-serial
- Olden
- PAQ8p (**C++**)
- Prolangs-C
- Ptrdist
- SciMark2-C
- Shootout
- sim
- Stanford
- tramp3d-v4 (**C++**)
- Trimaran
- TSVC
- VersaBench
- PolyBench

# Usage

TF contains a few environment variables that control its behavior:
* `COMPILE=1`: Compile a benchmark
* `EXEC=1`: Run a benchmark
* `INSTRUMENT=1`: Compile the benchmark with a custom optimization
    * `PASS=DAG`: The name of the optimization. In our case, the optimization is called `DAG`.
    * `PASS_OPT=`
        * `ess`: Just check if the store is silent
        * `eae`: Insert conditionals without profilling 
        * `alp`: eae + inner loop profilling
        * `plp`: eae + outer loop Profilling
* `JOBS=njobs`: Controls the number of simutaneous executions
* `DIFF=1`: Check the output

## Compiling Benchmarks

To compile a benchmark, type:
```
COMPILE=1 EXEC=0 ./run.sh
```

To compile and execute a single benchmark, type the same command with the relative path to the benchmark as argument:
```
COMPILE=1 EXEC=1 ./run.sh Benchs/Stanford/Quicksort/
```

To just compile a benchmark, set `EXEC=0`:
```
COMPILE=1 EXEC=0 ./run.sh Benchs/PolyBench/linear-algebra/solvers/lu
```

To apply our optimization to a benchmark, type:
```
COMPILE=1 EXEC=1 INSTRUMENT=1 PASS=DAG PASS_OPT=plp ./run.sh Benchs/PolyBench/linear-algebra/solvers/cholesky
```

A file with the name `run.log` is created at the root of the project containing the job runtime, the command used, the exit signal and some other information.

The variable `benchs` on file `benchs.sh` controls which benchmarks are compiled and executed by default. By default it contains all benchmarks available at the moment. 

# Research Questions

In this section, we provide some of the commands used in a few research questions.

## Prevalence

The pass `CountStores` will instrument every store in the program to record when the store was silent or not as well as if the store was marked or not. For instance, the command below instruments the program `Stanford/Quicksort` and produces a file called `store.txt` in the same directory.

```
$ COMPILE=1 EXEC=1 INSTRUMENT=1 PASS=CountStores ./run.sh Benchs/Stanford/Quicksort/

$ cat Benchs/Stanford/Quicksort/store.txt
id,marked,silent,total
0,0,0,0
1,0,0,100
2,0,0,500000
3,0,1,100
4,0,1,100
5,0,0,500000
6,0,0,500
7,0,0,400
8,0,132800,1563000
9,0,132800,1563000
```

## Speedup

For a given benchmark, to compare the runtimes with and without the optimization, run:

```
$ COMPILE=1 EXEC=1 ./run.sh Benchs/PolyBench/linear-algebra/solvers/cholesky

$ cat run.log
Seq     Host    Starttime       JobRuntime      Send    Receive Exitval Signal  Command
1       :       1597605558.267      66.910      0       0       0       0       cd /home/guilhermel/Programs/tf_phoenix/Benchs/PolyBench/linear-algebra/solvers/cholesky && timeout --signal=TERM 0 ./cholesky.exe   < /dev/null &> /dev/null

$ COMPILE=1 EXEC=1 INSTRUMENT=1 PASS=DAG PASS_OPT=plp ./run.sh Benchs/PolyBench/linear-algebra/solvers/cholesky

$ cat run.log
Seq     Host    Starttime       JobRuntime      Send    Receive Exitval Signal  Command
1       :       1597607159.770      32.927      0       0       0       0       cd /home/guilhermel/Programs/tf_phoenix/Benchs/PolyBench/linear-algebra/solvers/cholesky && timeout --signal=TERM 0 ./INS_cholesky.exe   < /dev/null &> /dev/null
```

# Problems

This artifact was not thoroughly tested. Thus, there may be some unnoticed
problems by the authors. At the moment, we are aware of the following problem.
The slicing algorithm used by the PLP version of semiring optimization assumes
a few properties:

- Loop must contain only one induction variable.
- Increment happens in the latch block.
- Loop must be of the form:
```
Loop Header --> Loop Body --> Loop Latch --+
   ^---------------------------------------|
```

If this algorithm is applied onto loops that do not meet this contract, 
then compilation will fail.

If you have further problems, e-mail: guilhermeleobas@gmail.com
