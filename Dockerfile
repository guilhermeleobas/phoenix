FROM ubuntu:16.04

RUN apt update -y

RUN apt install -y git wget xz-utils parallel gcc make cmake g++ vim

RUN git clone https://github.com/guilhermeleobas/phoenix
RUN git clone --branch phoenix --recursive https://github.com/guilhermeleobas/tf

RUN wget -O llvm.tar.xz https://releases.llvm.org/6.0.1/clang+llvm-6.0.1-x86_64-linux-gnu-ubuntu-16.04.tar.xz
RUN tar xf llvm.tar.xz && mv clang+llvm-6.0.1-x86_64-linux-gnu-ubuntu-16.04/ llvm61

RUN cd phoenix && CXX=/llvm61/bin/clang CMAKE_PREFIX_PATH=/llvm61 LLVM_DIR=/llvm61/build/lib/cmake bash rebuild.sh

RUN sed --in-place 's/LLVM_PATH=""/LLVM_PATH="\/llvm61\/bin"/' tf/vars.sh
RUN sed --in-place 's/PHOENIX_PATH=""/PHOENIX_PATH="\/phoenix"/' tf/vars.sh

RUN mkdir ~/.parallel
RUN touch ~/.parallel/will-cite

WORKDIR tf
