# Umicom Kernel native user-boundary tests.
# This separate project builds a host C executable, not a RISC-V guest. It uses
# the same allocator, page walker, user-buffer checks and dispatcher sources.
# QEMU is still required to prove privilege transitions and actual page faults.
cmake_minimum_required(VERSION 3.24)
project(UmicomUserMemoryTests LANGUAGES C)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
include(CTest)
option(UMICOM_USER_MEMORY_SANITIZERS "Run the native checks with address/undefined sanitizers" OFF)
add_executable(umicom-user-memory-tests
    user_memory_tests.c
    ../../kernel/address.c
    ../../kernel/physical_memory.c
    ../../kernel/virtual_memory.c
    ../../kernel/user_memory.c
    ../../kernel/user_monitor.c
)
target_include_directories(umicom-user-memory-tests PRIVATE ../../include)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(umicom-user-memory-tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    if(UMICOM_USER_MEMORY_SANITIZERS)
        target_compile_options(umicom-user-memory-tests PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(umicom-user-memory-tests PRIVATE -fsanitize=address,undefined)
    endif()
endif()
# Each case gets a new process and fresh allocator state. None of these test
# names claims that an emulated instruction or a real U-mode transition ran.
set(UMICOM_USER_NATIVE_CASES
    cross-page-read cross-page-write read-only-write supervisor-read
    unknown-page range-wrap noncanonical partial-destination oversized
    zero-length null-kernel-buffer wrong-backing-frame widened-permission
    unregistered-page invalid-access unknown-call assigned-identity
    checked-copy denied-copy exit-call call-budget deadline-event
    wrong-privilege next-pc-refusal)
foreach(UMICOM_USER_CASE IN LISTS UMICOM_USER_NATIVE_CASES)
    add_test(NAME kernel.user_boundary.${UMICOM_USER_CASE}
        COMMAND umicom-user-memory-tests ${UMICOM_USER_CASE})
endforeach()
