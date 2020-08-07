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
- **tf**: A list of scripts that will be used to evaluate the optimization proposed;
- **tf/Benchs**: The 287 benchmarks used in the experiments of this paper;

# TF - Testing Framework
TF contains a set of scripts we use to benchmark the optimization proposed.

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
* `RUN=1`: Run a benchmark
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
COMPILE=1 RUN=0 ./run.sh
```

To compile and execute a single benchmark, type the same command with the relative path to the benchmark as argument:
```
COMPILE=1 RUN=1 ./run.sh Benchs/Stanford/QuickSort
```

To apply our optimization to a benchmark, type:
```
COMPILE=1 RUN=1 INSTRUMENT=1 PASS=DAG PASS_OPT=plp ./run.sh Benchs/PoliBench/linear-algebra/solvers/cholesky
```

A file with the name `run.log` is created at the root of the project containing the job runtime, the command used, the exit signal and some other information.

The variable `benchs` on file `benchs.sh` controls which benchmarks are compiled and executed by default. By default it contains all benchmarks available at the moment. 

# Problems

This artifact was not thoroughly tested. Thus, there may be some unnoticed
problems by the authors. At the moment, we are aware of the following problem:

- There are some issues with the slicing algorithm used by the **plp** optimization since it assumes a certain loop shape. Thus, compilation may fail in some benchmarks.

If you have further problems, e-mail: guilhermeleobas@gmail.com
