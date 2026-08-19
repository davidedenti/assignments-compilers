cmake_minimum_required(VERSION 3.13)
project(LoopFusionPass)

# Trova un'installazione di LLVM (deve esporre LLVMConfig.cmake).
# Se necessario, passare -DLLVM_DIR=/path/to/llvm/lib/cmake/llvm a cmake.
find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

list(APPEND CMAKE_MODULE_PATH ${LLVM_CMAKE_DIR})
include(AddLLVM)

add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT LLVM_ENABLE_RTTI)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")
endif()

# Costruisce libLoopFusion.so, caricabile con:
#   opt -load-pass-plugin=./libLoopFusion.so -passes="loop-fusion" -S input.ll
add_llvm_pass_plugin(LoopFusion
  LoopFusion.cpp
)