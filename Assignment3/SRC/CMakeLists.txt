cmake_minimum_required(VERSION 3.20)
project(Assignment3 LANGUAGES C CXX)

find_package(LLVM 19.1 REQUIRED CONFIG)

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

add_llvm_pass_plugin(LoopInvariantMotion LoopInvariantMotion.cpp)