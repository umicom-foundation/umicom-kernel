#-----------------------------------------------------------------------------
# Umicom Kernel
# File: programs/cvm/CMakeLists.txt
#
# PURPOSE:
#   Connect the bounded CVM runtime to the native user-program contract.
#
# AUTHOR AND ORGANISATION:
# Sammy Hegab
# Umicom Foundation
#
# LICENCE:
# MIT
#---------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.24)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(UmicomKernelCvm LANGUAGES C ASM)
endif()
set(UMICOM_C_SOURCE_DIR "" CACHE PATH "Umicom C source checkout for the freestanding runtime")
if(NOT EXISTS "${UMICOM_C_SOURCE_DIR}/cmake/UmicomCRuntime.cmake")
    message(FATAL_ERROR "Set UMICOM_C_SOURCE_DIR to the Umicom C source checkout")
endif()
if(NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "riscv64" OR NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "Use the Kernel riscv64-clang toolchain for these freestanding ELF programs")
endif()
if(CMAKE_CONFIGURATION_TYPES)
    message(FATAL_ERROR "The Kernel embedded-ELF workflow requires a single-configuration generator")
endif()
include("${UMICOM_C_SOURCE_DIR}/cmake/UmicomCRuntime.cmake")
get_filename_component(_kernel "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(_flags -march=rv64imac_zicsr -mabi=lp64 -mcmodel=medany
    -msmall-data-limit=0 -ffreestanding -fno-builtin -fno-stack-protector
    -fno-pic -fno-pie -fno-unwind-tables -fno-asynchronous-unwind-tables
    -fno-jump-tables -fno-vectorize -fno-slp-vectorize
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
# Separate real runtime objects at O0 and O2 expose compiler-inserted libc
# dependencies. Neither link discards unused functions to hide such dependencies.
foreach(_mode IN ITEMS o0 o2)
    if(_mode STREQUAL "o0")
        set(_optimization -O0)
    else()
        set(_optimization -O2)
    endif()
    umicom_c_add_runtime("umicom_cvm_runtime_${_mode}")
    target_compile_options("umicom_cvm_runtime_${_mode}" PRIVATE ${_flags} ${_optimization})
    add_executable("umicom-cvm-user-${_mode}" entry.S main.c)
    target_compile_features("umicom-cvm-user-${_mode}" PRIVATE c_std_23)
    target_include_directories("umicom-cvm-user-${_mode}" PRIVATE "${_kernel}/include")
    target_compile_options("umicom-cvm-user-${_mode}" PRIVATE ${_flags} ${_optimization})
    target_link_libraries("umicom-cvm-user-${_mode}" PRIVATE "umicom_cvm_runtime_${_mode}")
    target_link_options("umicom-cvm-user-${_mode}" PRIVATE
        -march=rv64imac_zicsr -mabi=lp64 -mcmodel=medany -nostdlib -fuse-ld=lld
        "-Wl,-T,${_kernel}/programs/diagnostic/linker.ld"
        -Wl,--build-id=none -Wl,--no-relax)
    set_target_properties("umicom-cvm-user-${_mode}" PROPERTIES
        C_EXTENSIONS OFF OUTPUT_NAME "umicom-cvm-${_mode}" SUFFIX ".elf"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin"
        LINK_DEPENDS "${_kernel}/programs/diagnostic/linker.ld")
endforeach()
