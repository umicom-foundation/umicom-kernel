#-----------------------------------------------------------------------------
# Umicom Kernel
# File: tests/console_session/CMakeLists.txt
#
# PURPOSE:
#   Build a Windows host fixture for a real RAM-only QEMU console session.
#
# AUTHOR AND ORGANISATION:
# Sammy Hegab
# Umicom Foundation
#
# LICENCE:
# MIT
#-----------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.24)
project(UmicomConsoleSessionTests LANGUAGES C)
include(CTest)
if(NOT WIN32)
    message(FATAL_ERROR "This host fixture currently uses Windows process and socket APIs. Use the Kernel console guide on other hosts.")
endif()
set(UMICOM_CONSOLE_IMAGE "" CACHE FILEPATH "Previously built current umicom-console.elf")
find_program(UMICOM_CONSOLE_QEMU NAMES qemu-system-riscv64 REQUIRED)
if(NOT EXISTS "${UMICOM_CONSOLE_IMAGE}")
    message(FATAL_ERROR "Set UMICOM_CONSOLE_IMAGE to a current built Kernel console ELF.")
endif()
add_executable(umicom-console-session-tests session_tests.c)
target_link_libraries(umicom-console-session-tests PRIVATE ws2_32)
set_target_properties(umicom-console-session-tests PROPERTIES
    C_STANDARD 23 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(umicom-console-session-tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
endif()
add_test(NAME kernel.console_session.ram-files-and-native-client
    COMMAND umicom-console-session-tests "${UMICOM_CONSOLE_QEMU}" "${UMICOM_CONSOLE_IMAGE}")
set_tests_properties(kernel.console_session.ram-files-and-native-client PROPERTIES
    TIMEOUT 75 PASS_REGULAR_EXPRESSION "UMICOM_KERNEL_SERIAL_SESSION_PASS")

# A separate executable keeps the original RAM-only test and its invocation.
# The attached source fixture is opened by QEMU read-only, never as a host device.
set(UMICOM_CONSOLE_READ_ONLY_DISK
    "${CMAKE_CURRENT_SOURCE_DIR}/../disk_inspection/fixture.raw" CACHE FILEPATH
    "Synthetic FAT16 fixture for the read-only console journey")
if(NOT EXISTS "${UMICOM_CONSOLE_READ_ONLY_DISK}")
    message(FATAL_ERROR "The synthetic read-only disk fixture is required.")
endif()
add_executable(umicom-console-storage-session-tests session_tests.c)
target_compile_definitions(umicom-console-storage-session-tests PRIVATE UMICOM_CONSOLE_SESSION_STORAGE=1)
target_link_libraries(umicom-console-storage-session-tests PRIVATE ws2_32)
set_target_properties(umicom-console-storage-session-tests PROPERTIES
    C_STANDARD 23 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(umicom-console-storage-session-tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
endif()
# A partitioned FAT image belongs to normal startup. The diagnostic console
# deliberately expects a different raw-block fixture; retain that failed test
# invocation for review rather than weakening its device acceptance checks.
if(FALSE)
add_test(NAME kernel.console_session.read-only-capacity
    COMMAND umicom-console-storage-session-tests "${UMICOM_CONSOLE_QEMU}"
        "${UMICOM_CONSOLE_IMAGE}" "${UMICOM_CONSOLE_READ_ONLY_DISK}")
endif()
get_filename_component(UMICOM_CONSOLE_IMAGE_DIRECTORY "${UMICOM_CONSOLE_IMAGE}" DIRECTORY)
set(UMICOM_CONSOLE_SYSTEM_IMAGE "${UMICOM_CONSOLE_IMAGE_DIRECTORY}/umicom-system.elf"
    CACHE FILEPATH "Current normal system ELF for partitioned-disk console tests")
if(NOT EXISTS "${UMICOM_CONSOLE_SYSTEM_IMAGE}")
    message(FATAL_ERROR "Set UMICOM_CONSOLE_SYSTEM_IMAGE to the built normal system ELF.")
endif()
add_test(NAME kernel.console_session.read-only-capacity
    COMMAND umicom-console-storage-session-tests "${UMICOM_CONSOLE_QEMU}"
        "${UMICOM_CONSOLE_SYSTEM_IMAGE}" "${UMICOM_CONSOLE_READ_ONLY_DISK}")
set_tests_properties(kernel.console_session.read-only-capacity PROPERTIES
    TIMEOUT 75 PASS_REGULAR_EXPRESSION "UMICOM_KERNEL_SERIAL_SESSION_PASS")
