cmake_minimum_required(VERSION 3.20)
project(Assignment1 LANGUAGES C CXX)

find_package(LLVM 19.1 REQUIRED CONFIG)

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

set(LLVM_OPTIONAL_SOURCES
    AlgebricIdentity.cpp
    MultiInstructionOptimization.cpp
    StrengthReduction.cpp)

add_llvm_pass_plugin(AlgebricIdentity AlgebricIdentity.cpp)
add_llvm_pass_plugin(MultiInstructionOptimization MultiInstructionOptimization.cpp)
add_llvm_pass_plugin(StrengthReduction StrengthReduction.cpp)