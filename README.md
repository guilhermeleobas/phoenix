### This is a simple way to create a LLVM pass outside the LLVM tree

`build.sh` script will create a build folder, build the pass and copy the dylib/so file to the LLVM build folder

```bash
./build.sh

$ $HOME/Programs/llvm61/build/bin/opt -load MyPass.so -help | grep Pass
-MyLLVMPass                                     - My LLVM Pass description

```

![arnoldao](https://media.giphy.com/media/3otPoIRRO0TlyZAuT6/giphy-downsized.gif)
