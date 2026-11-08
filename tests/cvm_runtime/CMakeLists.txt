#-----------------------------------------------------------------------------
# Umicom Kernel
# File: tests/cvm_runtime/CMakeLists.txt
#
# PURPOSE:
#   Link the real guest entry with canonical Umicom C source for a separate host qualification layer.
#
# AUTHOR AND ORGANISATION:
# Sammy Hegab
# Umicom Foundation
#
# LICENCE:
# MIT
#-----------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.24)
project(UmicomCvmRuntimeEnvelope LANGUAGES C)
include(CTest)
set(UMICOM_C_SOURCE_DIR "" CACHE PATH "Canonical Umicom C source checkout")
if(NOT EXISTS "${UMICOM_C_SOURCE_DIR}/cmake/UmicomCRuntime.cmake")
    message(FATAL_ERROR "Set UMICOM_C_SOURCE_DIR to the comparison-capable Umicom C checkout")
endif()
include("${UMICOM_C_SOURCE_DIR}/cmake/UmicomCRuntime.cmake")
umicom_c_add_runtime(umicom_cvm_host_runtime)
add_executable(umicom-cvm-envelope test_envelope.c ../../programs/cvm/main.c)
target_include_directories(umicom-cvm-envelope PRIVATE ../../include)
target_compile_features(umicom-cvm-envelope PRIVATE c_std_23)
target_link_libraries(umicom-cvm-envelope PRIVATE umicom_cvm_host_runtime)
set_target_properties(umicom-cvm-envelope PROPERTIES C_EXTENSIONS OFF)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(umicom-cvm-envelope PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
endif()
if(BUILD_TESTING)
    foreach(case IN ITEMS arithmetic fuel fault reserved-flags reserved-selector trace trace-fuel trace-fault trace-capacity comparison-true comparison-false comparison-version comparison-fuel unknown-selector maximum-selector)
        add_test(NAME "kernel.cvm.envelope.${case}" COMMAND umicom-cvm-envelope "${case}")
        set_tests_properties("kernel.cvm.envelope.${case}" PROPERTIES TIMEOUT 10 LABELS "kernel;cvm;host;consumer")
    endforeach()
endif()
