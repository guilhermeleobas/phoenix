#!/bin/bash

# this should point out to where your LLVM BUILD folder
export LLVM_BUILD_DIR=$HOME/Programs/llvm61/build
# this should point out to the cmake folder inside the build folder
export LLVM_DIR=$LLVM_BUILD_DIR/lib/cmake

rm -rf build
mkdir build
cd build
cmake -DCMAKE_PREFIX_PATH=$LLVM_BUILD_DIR ..
make

cp $(pwd)/Pass/MyPass.* $LLVM_BUILD_DIR/lib/
