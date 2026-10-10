#-----------------------------------------------------------------------------
# Umicom Kernel: host-side read-only FAT16 recovery evidence
# Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
#
# This intentionally configures separately from the freestanding RV64 Kernel:
# no existing kernel target, code path or test is replaced.
#-----------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.24)
project(UmicomFat16RecoveryAudit LANGUAGES C)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
include(CTest)

add_library(umicom-fat16-audit-core STATIC fat16_recovery_audit.c)
target_include_directories(umicom-fat16-audit-core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
add_executable(umicom-fat16-recovery-audit main.c)
target_link_libraries(umicom-fat16-recovery-audit PRIVATE umicom-fat16-audit-core)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    foreach(targetName IN ITEMS umicom-fat16-audit-core umicom-fat16-recovery-audit)
        target_compile_options(${targetName} PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    endforeach()
endif()

if(BUILD_TESTING)
    add_executable(umicom-fat16-recovery-tests tests/recovery_tests.c)
    target_link_libraries(umicom-fat16-recovery-tests PRIVATE umicom-fat16-audit-core)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(umicom-fat16-recovery-tests PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    endif()
    add_test(NAME kernel.host.fat16_recovery_audit COMMAND umicom-fat16-recovery-tests)

    # The CLI tests use disposable sparse images inside this subproject's own
    # build output, not a production volume or an existing user image.
    add_executable(umicom-fat16-audit-make-fixtures tests/make_fixtures.c)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(umicom-fat16-audit-make-fixtures PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    endif()
    set(UMICOM_FAT16_CLEAN_TEST_IMAGE "${CMAKE_CURRENT_BINARY_DIR}/audit-clean.raw")
    set(UMICOM_FAT16_DIRTY_TEST_IMAGE "${CMAKE_CURRENT_BINARY_DIR}/audit-dirty.raw")
    set(UMICOM_FAT16_MISMATCH_TEST_IMAGE "${CMAKE_CURRENT_BINARY_DIR}/audit-mismatch.raw")
    add_test(NAME kernel.host.fat16_fixture_prepare
        COMMAND umicom-fat16-audit-make-fixtures
            "${UMICOM_FAT16_CLEAN_TEST_IMAGE}"
            "${UMICOM_FAT16_DIRTY_TEST_IMAGE}"
            "${UMICOM_FAT16_MISMATCH_TEST_IMAGE}")
    set_tests_properties(kernel.host.fat16_fixture_prepare PROPERTIES
        FIXTURES_SETUP fat16_audit_test_images)
    add_test(NAME kernel.host.fat16_cli_clean
        COMMAND umicom-fat16-recovery-audit "${UMICOM_FAT16_CLEAN_TEST_IMAGE}")
    set_tests_properties(kernel.host.fat16_cli_clean PROPERTIES
        FIXTURES_REQUIRED fat16_audit_test_images
        PASS_REGULAR_EXPRESSION "clean-mirrored-fat")
    add_test(NAME kernel.host.fat16_cli_dirty
        COMMAND umicom-fat16-recovery-audit "${UMICOM_FAT16_DIRTY_TEST_IMAGE}")
    set_tests_properties(kernel.host.fat16_cli_dirty PROPERTIES
        FIXTURES_REQUIRED fat16_audit_test_images
        WILL_FAIL TRUE)
    add_test(NAME kernel.host.fat16_cli_mismatch
        COMMAND umicom-fat16-recovery-audit "${UMICOM_FAT16_MISMATCH_TEST_IMAGE}")
    set_tests_properties(kernel.host.fat16_cli_mismatch PROPERTIES
        FIXTURES_REQUIRED fat16_audit_test_images
        WILL_FAIL TRUE)
endif()

# Read-only namespace/allocation verification is an optional separate command.
# Keep the original header audit and its CLI unchanged; the additional tool
# reuses admission evidence, but never enables disk writes or mount operations.
add_library(umicom-fat16-integrity-core STATIC fat16_integrity_audit.c)
target_link_libraries(umicom-fat16-integrity-core PUBLIC umicom-fat16-audit-core)
target_include_directories(umicom-fat16-integrity-core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
add_executable(umicom-fat16-integrity-audit integrity_main.c)
target_link_libraries(umicom-fat16-integrity-audit PRIVATE umicom-fat16-integrity-core)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    foreach(targetName IN ITEMS umicom-fat16-integrity-core umicom-fat16-integrity-audit)
        target_compile_options(${targetName} PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    endforeach()
endif()

if(BUILD_TESTING)
    add_executable(umicom-fat16-integrity-tests tests/integrity_tests.c)
    target_link_libraries(umicom-fat16-integrity-tests PRIVATE umicom-fat16-integrity-core)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(umicom-fat16-integrity-tests PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    endif()
    add_test(NAME kernel.host.fat16_integrity_graph COMMAND umicom-fat16-integrity-tests)
    add_test(NAME kernel.host.fat16_integrity_cli_clean
        COMMAND umicom-fat16-integrity-audit "${UMICOM_FAT16_CLEAN_TEST_IMAGE}" 0)
    set_tests_properties(kernel.host.fat16_integrity_cli_clean PROPERTIES
        FIXTURES_REQUIRED fat16_audit_test_images
        PASS_REGULAR_EXPRESSION "consistent-allocation-graph")
    add_test(NAME kernel.host.fat16_integrity_cli_dirty_refused
        COMMAND umicom-fat16-integrity-audit "${UMICOM_FAT16_DIRTY_TEST_IMAGE}" 0)
    set_tests_properties(kernel.host.fat16_integrity_cli_dirty_refused PROPERTIES
        FIXTURES_REQUIRED fat16_audit_test_images WILL_FAIL TRUE)
    add_test(NAME kernel.host.fat16_integrity_cli_mismatch_refused
        COMMAND umicom-fat16-integrity-audit "${UMICOM_FAT16_MISMATCH_TEST_IMAGE}" 0)
    set_tests_properties(kernel.host.fat16_integrity_cli_mismatch_refused PROPERTIES
        FIXTURES_REQUIRED fat16_audit_test_images WILL_FAIL TRUE)
endif()

# Test results must correspond to the current source: a failed compile must
# not let CTest run an executable linked from an older successful build.
# CTest fixtures order the build ahead of both existing and new host journeys.
if(BUILD_TESTING)
    add_test(NAME kernel.host.fat16_current_binaries
        COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}" --parallel 2)
    set_tests_properties(kernel.host.fat16_current_binaries PROPERTIES
        FIXTURES_SETUP fat16_audit_current_binaries TIMEOUT 180)
    foreach(testName IN ITEMS
            kernel.host.fat16_recovery_audit
            kernel.host.fat16_fixture_prepare
            kernel.host.fat16_cli_clean
            kernel.host.fat16_cli_dirty
            kernel.host.fat16_cli_mismatch
            kernel.host.fat16_integrity_graph
            kernel.host.fat16_integrity_cli_clean
            kernel.host.fat16_integrity_cli_dirty_refused
            kernel.host.fat16_integrity_cli_mismatch_refused)
        set_property(TEST ${testName} APPEND PROPERTY
            FIXTURES_REQUIRED fat16_audit_current_binaries)
    endforeach()
endif()

# The interruption laboratory is a deliberately independent host-side
# durability MODEL. It allocates synthetic images privately and calls both
# existing read-only auditors after each modelled loss-of-power boundary.
# No Kernel target, writer, repair path or existing audit CLI is replaced.
add_library(umicom-fat16-interruption-core STATIC fat16_interruption_lab.c)
target_link_libraries(umicom-fat16-interruption-core PUBLIC umicom-fat16-integrity-core)
target_include_directories(umicom-fat16-interruption-core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
add_executable(umicom-fat16-interruption-lab interruption_main.c)
target_link_libraries(umicom-fat16-interruption-lab PRIVATE umicom-fat16-interruption-core)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    foreach(targetName IN ITEMS umicom-fat16-interruption-core umicom-fat16-interruption-lab)
        target_compile_options(${targetName} PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    endforeach()
endif()

if(BUILD_TESTING)
    add_executable(umicom-fat16-interruption-tests tests/interruption_tests.c)
    target_link_libraries(umicom-fat16-interruption-tests PRIVATE umicom-fat16-interruption-core)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(umicom-fat16-interruption-tests PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    endif()
    add_test(NAME kernel.host.fat16_interruption_model
        COMMAND umicom-fat16-interruption-tests)
    add_test(NAME kernel.host.fat16_interruption_matrix
        COMMAND umicom-fat16-interruption-lab --matrix)
    set_tests_properties(kernel.host.fat16_interruption_matrix PROPERTIES
        PASS_REGULAR_EXPRESSION "\"scenarios\":28")
    add_test(NAME kernel.host.fat16_interruption_writeback_guard
        COMMAND umicom-fat16-interruption-lab --scenario write-back 2)
    set_tests_properties(kernel.host.fat16_interruption_writeback_guard PROPERTIES
        PASS_REGULAR_EXPRESSION "fat-mirror-mismatch")
    add_test(NAME kernel.host.fat16_interruption_writethrough_partial
        COMMAND umicom-fat16-interruption-lab --scenario write-through 5)
    set_tests_properties(kernel.host.fat16_interruption_writethrough_partial PROPERTIES
        PASS_REGULAR_EXPRESSION "\"file_data\":\"mixed\"")
    add_test(NAME kernel.host.fat16_interruption_final_ack_lost
        COMMAND umicom-fat16-interruption-lab --scenario write-back 12 --lost-ack)
    set_tests_properties(kernel.host.fat16_interruption_final_ack_lost PROPERTIES
        PASS_REGULAR_EXPRESSION "\"final_ack_lost\":true")
    add_test(NAME kernel.host.fat16_interruption_clean_hybrid
        COMMAND umicom-fat16-interruption-lab --hybrid)
    set_tests_properties(kernel.host.fat16_interruption_clean_hybrid PROPERTIES
        PASS_REGULAR_EXPRESSION "\"file_data\":\"mixed\"")
    add_test(NAME kernel.host.fat16_interruption_reject_bad_cutoff
        COMMAND umicom-fat16-interruption-lab --scenario write-back 13)
    set_tests_properties(kernel.host.fat16_interruption_reject_bad_cutoff PROPERTIES
        WILL_FAIL TRUE)
    foreach(testName IN ITEMS
            kernel.host.fat16_interruption_model
            kernel.host.fat16_interruption_matrix
            kernel.host.fat16_interruption_writeback_guard
            kernel.host.fat16_interruption_writethrough_partial
            kernel.host.fat16_interruption_final_ack_lost
            kernel.host.fat16_interruption_clean_hybrid
            kernel.host.fat16_interruption_reject_bad_cutoff)
        set_property(TEST ${testName} APPEND PROPERTY
            FIXTURES_REQUIRED fat16_audit_current_binaries)
    endforeach()
endif()
