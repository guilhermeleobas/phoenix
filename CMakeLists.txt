cmake_minimum_required(VERSION 3.4)

file(REMOVE_RECURSE build)

add_subdirectory(Profile)
add_subdirectory(CountArith)
add_subdirectory(Collect)
add_subdirectory(Identify)
add_subdirectory(Optimize)
add_subdirectory(DAG)
