#-----------------------------------------------------------------------------
# Umicom Kernel
# File: cmake/toolchains/riscv64-clang.cmake
#
# PURPOSE:
#   Configure CMake to cross-compile freestanding RV64 code with LLVM/Clang.
#   The file selects compilers only; target-specific warning, ABI and linker
#   options remain visible in the top-level build definition.
#
# AUTHOR AND ORGANISATION:
#   Sammy Hegab
#   Umicom Foundation
#
# LICENCE:
#   MIT
#-----------------------------------------------------------------------------

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv64)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_C_COMPILER clang)
set(CMAKE_ASM_COMPILER clang)
set(CMAKE_C_COMPILER_TARGET riscv64-unknown-elf)
set(CMAKE_ASM_COMPILER_TARGET riscv64-unknown-elf)
