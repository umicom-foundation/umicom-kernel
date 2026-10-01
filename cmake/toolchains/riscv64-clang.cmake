#-----------------------------------------------------------------------------
# Umicom Kernel
# File: cmake/toolchains/riscv64-clang.cmake
#
# PURPOSE:
#   Teach CMake that the build machine is Windows/Linux/etc. but the generated
#   machine code is for a freestanding 64-bit RISC-V target.
#
# EDUCATIONAL NOTE:
#   A "cross compiler" creates machine code for a processor/operating
#   environment different from the one running the compiler.  Here Clang runs
#   on the developer's PC but emits RISC-V ELF object files.
#
# AUTHOR AND ORGANISATION:
#   Sammy Hegab
#   Umicom Foundation
#
# LICENCE:
#   MIT
#-----------------------------------------------------------------------------

# "Generic" tells CMake there is no hosted target operating system providing
# normal executable-link/run facilities.
set(CMAKE_SYSTEM_NAME Generic)

# Record the target CPU architecture for CMake conditions and diagnostics.
set(CMAKE_SYSTEM_PROCESSOR riscv64)

# CMake normally probes a compiler by building and linking a tiny executable.
# A freestanding kernel toolchain may not have normal hosted startup/runtime
# objects, so compiler probes should stop after producing a static library.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Search first through PATH, then through two common Windows LLVM locations.
# MSYS2 UCRT64 is the recommended beginner setup for this repository because
# the same environment can also provide LLD, CMake, Ninja and QEMU.
find_program(
    UMICOM_CLANG_EXECUTABLE
    NAMES
        clang
        clang.exe
    HINTS
        "C:/msys64/ucrt64/bin"
        "C:/Program Files/LLVM/bin"
    REQUIRED
)

# Find the LLVM linker separately so configuration can fail early with a clear
# error if Clang is present but LLD was not installed.
find_program(
    UMICOM_LLD_EXECUTABLE
    NAMES
        ld.lld
        ld.lld.exe
    HINTS
        "C:/msys64/ucrt64/bin"
        "C:/Program Files/LLVM/bin"
    REQUIRED
)

# Use the located Clang binary for compiling C source.
set(CMAKE_C_COMPILER "${UMICOM_CLANG_EXECUTABLE}")

# Use the same Clang front end for preprocessed Assembly (.S) source.
set(CMAKE_ASM_COMPILER "${UMICOM_CLANG_EXECUTABLE}")

# Ask Clang to emit code for a bare-metal RISC-V 64 target instead of the host.
set(CMAKE_C_COMPILER_TARGET riscv64-unknown-elf)

# Apply the same target triple when compiling the Assembly bootstrap.
set(CMAKE_ASM_COMPILER_TARGET riscv64-unknown-elf)

# Publish the located linker to CMake for diagnostics/tools.  The final link is
# still driven by Clang with "-fuse-ld=lld", which supplies the correct target.
set(CMAKE_LINKER "${UMICOM_LLD_EXECUTABLE}")
