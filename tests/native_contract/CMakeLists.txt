# Umicom Kernel
# File: tests/native_contract/CMakeLists.txt
#
# PURPOSE:
#   Build a focused host check for the freestanding native-program ABI client.
#
# AUTHOR AND ORGANISATION:
# Sammy Hegab
# Umicom Foundation
#
# LICENCE:
# MIT
cmake_minimum_required(VERSION 3.24)
project(UmicomNativeContractTests LANGUAGES C)
include(CTest)
add_executable(umicom-native-contract-tests
    contract_tests.c ../../programs/support/native_contract.c)
target_include_directories(umicom-native-contract-tests PRIVATE ../../include)
set_target_properties(umicom-native-contract-tests PROPERTIES
    C_STANDARD 23 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(umicom-native-contract-tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
endif()
foreach(case current minimal future-minor unknown-features null-bridge null-output
    legacy absent future-major version-overflow word-width byte-order page-size
    copy-zero copy-too-large call-budget unavailable-1 unavailable-2 unavailable-3
    unavailable-4 unavailable-5 unavailable-6)
    add_test(NAME kernel.native_contract.${case} COMMAND umicom-native-contract-tests ${case})
    set_tests_properties(kernel.native_contract.${case} PROPERTIES TIMEOUT 10)
endforeach()

add_executable(umicom-native-requirements-tests
    requirements_tests.c ../../programs/support/native_contract.c)
target_include_directories(umicom-native-requirements-tests PRIVATE ../../include)
set_target_properties(umicom-native-requirements-tests PROPERTIES
    C_STANDARD 23 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(umicom-native-requirements-tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
endif()
foreach(case exact-limits null-contract null-requirements short-prefix unknown-tail
    minor-invalid minor-newer minor-supported missing-feature unknown-feature-needed
    unknown-feature-present copy-too-large no-copy-needed used-too-many one-too-many
    sum-overflow used-overflow budget-zero-remaining exit-still-needed
    malformed-version malformed-features malformed-machine malformed-page
    malformed-order malformed-copy malformed-zero-copy malformed-budget
    malformed-unavailable-budget)
    add_test(NAME kernel.native_requirements.${case}
        COMMAND umicom-native-requirements-tests ${case})
    set_tests_properties(kernel.native_requirements.${case} PROPERTIES TIMEOUT 10)
endforeach()
