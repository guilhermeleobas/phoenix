cmake_minimum_required(VERSION 3.4)

find_package(LLVM REQUIRED CONFIG)
add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS})
link_directories(${LLVM_LIBRARY_DIRS})

list(APPEND CMAKE_MODULE_PATH "${LLVM_DIR}")
include(AddLLVM)

add_llvm_loadable_module(Instrument Instrument.cpp)

# Use C++11 to compile your pass (i.e., supply -std=c++11).
target_compile_features(Instrument PRIVATE cxx_range_for cxx_auto_type)

# LLVM is (typically) built with no C++ RTTI. We need to match that;
# otherwise, we'll get linker errors about missing RTTI data.
set_target_properties(Instrument PROPERTIES
  COMPILE_FLAGS "-fno-rtti"
)

# Get proper shared-library behavior (where symbols are not necessarily
# resolved when the shared library is linked) on OS X.
if(APPLE)
  set_target_properties(Instrument PROPERTIES
    LINK_FLAGS "-undefined dynamic_lookup"
  )
endif(APPLE)
