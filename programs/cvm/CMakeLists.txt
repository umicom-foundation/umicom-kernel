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
if(NOT EXISTS "${UMICOM_C_SOURCE_DIR}/include/umicom_c/trace.h")
    message(FATAL_ERROR "The CVM diagnostic requires Umicom C source with the caller-owned trace API")
endif()
# The profile selector calls an additive runtime API, so refuse an older SDK
# at configuration before a missing header becomes an opaque compiler failure.
if(NOT EXISTS "${UMICOM_C_SOURCE_DIR}/include/umicom_c/profile.h")
    message(FATAL_ERROR "The CVM diagnostic requires Umicom C linked-profile queries")
endif()
foreach(required include/umicom_c/machine.h include/umicom_c/managed_heap.h src/managed_heap.c
        include/umicom_c/execution_bounds.h src/execution_bounds.c)
    if(NOT EXISTS "${UMICOM_C_SOURCE_DIR}/${required}")
        message(FATAL_ERROR "CVM requires the resumable and managed-heap SDK: ${required}")
    endif()
endforeach()
include("${UMICOM_C_SOURCE_DIR}/cmake/UmicomCRuntime.cmake")
get_filename_component(_kernel "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(UMICOM_KERNEL_CVM_HOST_COMPILER "" CACHE FILEPATH
    "Optional hosted Umicom C executable for the Kernel artifact diagnostic")
if(UMICOM_KERNEL_CVM_HOST_COMPILER)
    if(NOT IS_ABSOLUTE "${UMICOM_KERNEL_CVM_HOST_COMPILER}"
            OR NOT EXISTS "${UMICOM_KERNEL_CVM_HOST_COMPILER}"
            OR IS_DIRECTORY "${UMICOM_KERNEL_CVM_HOST_COMPILER}")
        message(FATAL_ERROR "Select an existing absolute UMICOM_KERNEL_CVM_HOST_COMPILER executable")
    endif()
    set(UMICOM_KERNEL_CVM_HOST_ARTIFACT_FILE "${CMAKE_CURRENT_BINARY_DIR}/generated/host-fixture.umcb")
    set(_host_source "${CMAKE_CURRENT_SOURCE_DIR}/host_fixture.uc")
    set(_host_prepare "${_kernel}/cmake/PrepareCvmHostArtifact.cmake")
    add_custom_command(OUTPUT "${UMICOM_KERNEL_CVM_HOST_ARTIFACT_FILE}" "${UMICOM_KERNEL_CVM_HOST_ARTIFACT_FILE}.receipt"
        COMMAND "${CMAKE_COMMAND}" "-DCOMPILER=${UMICOM_KERNEL_CVM_HOST_COMPILER}"
            "-DINPUT=${_host_source}" "-DOUTPUT=${UMICOM_KERNEL_CVM_HOST_ARTIFACT_FILE}"
            -P "${_host_prepare}"
        DEPENDS "${UMICOM_KERNEL_CVM_HOST_COMPILER}" "${_host_source}" "${_host_prepare}"
        VERBATIM)
    add_custom_target(umicom-cvm-host-artifact DEPENDS
        "${UMICOM_KERNEL_CVM_HOST_ARTIFACT_FILE}" "${UMICOM_KERNEL_CVM_HOST_ARTIFACT_FILE}.receipt")
    set(_host_embedding "${CMAKE_CURRENT_BINARY_DIR}/generated/host_artifact.S")
    configure_file(host_artifact.S.in "${_host_embedding}" @ONLY)
    set_source_files_properties("${_host_embedding}"
        PROPERTIES OBJECT_DEPENDS "${UMICOM_KERNEL_CVM_HOST_ARTIFACT_FILE}")
    if(NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
        set(UMICOM_KERNEL_CVM_HOST_ARTIFACT_FILE "${UMICOM_KERNEL_CVM_HOST_ARTIFACT_FILE}" PARENT_SCOPE)
    endif()
endif()
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
    # This diagnostic explicitly opts into the separate managed-object library.
    target_sources("umicom_cvm_runtime_${_mode}" PRIVATE "${UMICOM_C_SOURCE_DIR}/src/managed_heap.c")
    target_compile_options("umicom_cvm_runtime_${_mode}" PRIVATE ${_flags} ${_optimization})
    add_executable("umicom-cvm-user-${_mode}" entry.S main.c)
    if(UMICOM_KERNEL_CVM_HOST_ARTIFACT_FILE)
        target_sources("umicom-cvm-user-${_mode}" PRIVATE "${_host_embedding}")
        target_compile_definitions("umicom-cvm-user-${_mode}" PRIVATE UMICOM_KERNEL_CVM_HOST_ARTIFACT=1)
        add_dependencies("umicom-cvm-user-${_mode}" umicom-cvm-host-artifact)
    endif()
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
