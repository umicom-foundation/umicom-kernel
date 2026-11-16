# Umicom Kernel
# File: tests/virtual_memory/CMakeLists.txt
#
# PURPOSE:
#   Qualify physical-width admission against the real hosted Kernel mapper and allocator.
#
# AUTHOR AND ORGANISATION:
# Sammy Hegab
# Umicom Foundation
#
# LICENCE:
# MIT


cmake_minimum_required(VERSION 3.24)
project(UmicomVirtualMemoryTests LANGUAGES C)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
include(CTest)
add_executable(umicom-virtual-memory-tests physical_address_tests.c
    ../../kernel/address.c ../../kernel/physical_memory.c ../../kernel/virtual_memory.c)
target_include_directories(umicom-virtual-memory-tests PRIVATE ../../include)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(umicom-virtual-memory-tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
endif()
foreach(mode width-query highest-leaf zero-leaf high-virtual-leaf high-bits-refused existing-map-preserved invalid-input-order table-allocation-first-fit root-high-range root-high-reserved root-crossing-limit root-exhausted map-exhaustion-rollback status-values)
    add_test(NAME kernel.virtual_memory.physical_width.${mode} COMMAND umicom-virtual-memory-tests "${mode}")
    set_tests_properties(kernel.virtual_memory.physical_width.${mode} PROPERTIES
        TIMEOUT 30 LABELS "kernel;virtual-memory;physical-address;hosted")
endforeach()


# Stored entries require validation independently of caller physical-address admission.
add_executable(umicom-virtual-entry-tests entry_encoding_tests.c
    ../../kernel/address.c ../../kernel/physical_memory.c ../../kernel/virtual_memory.c)
target_include_directories(umicom-virtual-entry-tests PRIVATE ../../include)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(umicom-virtual-entry-tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
endif()
function(umicom_entry_case mode level value)
    set(name "kernel.virtual_memory.entry.${mode}.${level}.${value}")
    add_test(NAME "${name}" COMMAND umicom-virtual-entry-tests "${mode}" "${level}" "${value}")
    set_tests_properties("${name}" PROPERTIES TIMEOUT 30 LABELS "kernel;virtual-memory;entry-encoding;hosted")
endfunction()
foreach(level RANGE 0 2)
    foreach(bit RANGE 54 63)
        umicom_entry_case(reserved "${level}" "${bit}")
    endforeach()
    foreach(bit 8 9)
        umicom_entry_case(software "${level}" "${bit}")
    endforeach()
endforeach()
foreach(level RANGE 0 1)
    foreach(bit 2 4 5 6 7)
        umicom_entry_case(nonleaf "${level}" "${bit}")
    endforeach()
    umicom_entry_case(large "${level}" 1)
endforeach()
foreach(rights RANGE 0 7)
    umicom_entry_case(rights 2 "${rights}")
endforeach()
umicom_entry_case(invalid-payload 0 0)
