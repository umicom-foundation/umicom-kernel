# Umicom Kernel VFS and RAMFS host regression suite.
# The real providers and allocator run in a page-aligned host arena. Only the
# machine gate and deliberately refused frame allocations/releases are models.
# Sammy Hegab, Umicom Foundation. MIT licence.
cmake_minimum_required(VERSION 3.24)
project(UmicomVfsTests LANGUAGES C)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
include(CTest)
option(UMICOM_VFS_SANITIZERS "Enable address and undefined behaviour sanitizers" OFF)
add_executable(umicom-vfs-tests ramfs_tests.c
    ../../kernel/vfs.c ../../kernel/ramfs.c ../../kernel/object_cache.c
    ../../kernel/physical_memory.c ../../kernel/address.c)
target_include_directories(umicom-vfs-tests PRIVATE ../../include)
# Only the allocator definition is renamed. The wrappers still invoke its real
# bitmap implementation except at the explicitly selected refusal boundary.
set_source_files_properties(../../kernel/physical_memory.c PROPERTIES COMPILE_DEFINITIONS
    "UmicomKernelPhysicalMemoryAllocateFrame=UmicomVfsActualAllocate;UmicomKernelPhysicalMemoryFreeFrame=UmicomVfsActualFree")
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(umicom-vfs-tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    if(UMICOM_VFS_SANITIZERS)
        target_compile_options(umicom-vfs-tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(umicom-vfs-tests PRIVATE -fsanitize=address,undefined)
    endif()
endif()
set(UMICOM_VFS_CASES
    lazy-initialisation reinitialisation-refused copied-owner empty-directory
    nested-paths invalid-paths component-boundary depth-boundary duplicate-create
    file-as-directory nonempty-directory open-directory-removal descriptor-types
    client-rights descriptor-rights duplicate-rights shared-offset independent-offset
    client-local-tokens stale-descriptor generation-exhaustion descriptor-capacity
    client-capacity eof partial-eof binary-bytes cross-page-io noncontiguous-pages
    sparse-gap sparse-growth shrink-regrow-zeroes append maximum-file
    partial-allocation-failure data-page-quota node-quota unlink-open-recreate
    reap-pinned-file reap-release-retry close-release-positions close-busy
    client-close-pins iteration-change entry-snapshot root-removal null-arguments
    range-overflow zero-io unsafe-context missing-backing-frame aliased-backing-frame
    invalid-parent damaged-cache-guard descriptor-count-corruption epoch-exhaustion
    identity-exhaustion multiple-mount-pins repeated-lifetimes mixed-byte-model
    allocation-budgets missing-provider-operation outputs-on-refusal metadata-data-alias
    cache-release-retry directory-duplicate-cursor)
foreach(UMICOM_VFS_CASE IN LISTS UMICOM_VFS_CASES)
    add_test(NAME kernel.vfs.${UMICOM_VFS_CASE} COMMAND umicom-vfs-tests ${UMICOM_VFS_CASE})
endforeach()

# Supply a separately cross-built diagnostic ELF to add a real byte-format and
# loader integration test. Its absence is reported, never counted as a pass.
set(UMICOM_VFS_TEST_EXECUTABLE "" CACHE FILEPATH "Existing RV64 diagnostic ELF for byte-level loader integration")
if(UMICOM_VFS_TEST_EXECUTABLE)
    if(NOT EXISTS "${UMICOM_VFS_TEST_EXECUTABLE}")
        message(FATAL_ERROR "The supplied diagnostic ELF does not exist")
    endif()
    add_executable(umicom-vfs-loader-test loader_bridge.c
        ../../kernel/vfs.c ../../kernel/ramfs.c ../../kernel/object_cache.c
        ../../kernel/physical_memory.c ../../kernel/address.c
        ../../kernel/executable.c ../../kernel/process_image.c
        ../../kernel/virtual_memory.c ../../kernel/user_memory.c)
    target_include_directories(umicom-vfs-loader-test PRIVATE ../../include)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(umicom-vfs-loader-test PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
        if(UMICOM_VFS_SANITIZERS)
            target_compile_options(umicom-vfs-loader-test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(umicom-vfs-loader-test PRIVATE -fsanitize=address,undefined)
        endif()
    endif()
    add_test(NAME kernel.vfs.actual-elf-file-bridge COMMAND umicom-vfs-loader-test "${UMICOM_VFS_TEST_EXECUTABLE}")
else()
    message(STATUS "No diagnostic ELF supplied: the optional file-to-loader test is not registered")
endif()
