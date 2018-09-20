cmake_minimum_required(VERSION 3.4)

file(REMOVE_RECURSE build)

add_subdirectory(Instrument)
add_subdirectory(Count)
add_subdirectory(Collect)
