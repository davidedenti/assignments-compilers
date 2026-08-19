cmake_minimum_required(VERSION 3.20)
project(Assignment4 LANGUAGES C CXX)

find_package(LLVM 19.1 REQUIRED CONFIG)

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Ogni assignment viene compilato nella propria directory di build.
add_llvm_pass_plugin(LocalOpts LoopFusion.cpp)

# Mantiene le informazioni LLVM nel target che contiene LoopFusion.cpp, così
# anche gli strumenti CMake di CLion ricevono gli stessi include e flag.
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_include_directories(LocalOpts SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
target_compile_options(LocalOpts PRIVATE ${LLVM_DEFINITIONS_LIST})
