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
