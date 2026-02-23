cmake_minimum_required(VERSION 3.14)
project(rainbow)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include FetchContent module
include(FetchContent)

# Fetch ParlayLib
FetchContent_Declare(
  parlaylib
  GIT_REPOSITORY https://github.com/cmuparlay/parlaylib.git
  GIT_TAG master
)

FetchContent_MakeAvailable(parlaylib)

# PG-MRM: Parallel Greedy Maximal Rainbow Matching
add_executable(rainbow rainbow.cpp)
target_link_libraries(rainbow PRIVATE parlay)

# Graph generator
add_executable(generate_erdos_renyi generate_erdos_renyi.cpp)
