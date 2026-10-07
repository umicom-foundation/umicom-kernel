# Umicom Kernel isolated WRITE/FLUSH and fresh-process readback qualification.
# The existing fixture remains immutable input. A CTest setup makes a distinct
# raw copy; the writer and cold reader share only that disposable copy. QEMU's
# snapshot mode forces unsafe caching, so these tests use explicit writeback
# caching with host flushes enabled and delete the copy through fixture cleanup.
# Author: Sammy Hegab, Umicom Foundation. Licence: MIT.

function(UmicomConfigureWritableBlockImages)
    if(NOT BUILD_TESTING)
        return()
    endif()
    get_target_property(UMICOM_WRITE_SOURCES umicom-kernel SOURCES)
    get_target_property(UMICOM_WRITE_INCLUDES umicom-kernel INCLUDE_DIRECTORIES)
    get_target_property(UMICOM_WRITE_OPTIONS umicom-kernel COMPILE_OPTIONS)
    get_target_property(UMICOM_WRITE_LINK_OPTIONS umicom-kernel LINK_OPTIONS)
    get_target_property(UMICOM_WRITE_DEFINITIONS umicom-kernel COMPILE_DEFINITIONS)
    get_target_property(UMICOM_WRITE_LIBRARIES umicom-kernel LINK_LIBRARIES)
    get_target_property(UMICOM_WRITE_DEPENDENCIES umicom-kernel MANUALLY_ADDED_DEPENDENCIES)
    foreach(UMICOM_WRITE_ROLE IN ITEMS writer readback)
        if(UMICOM_WRITE_ROLE STREQUAL "writer")
            set(UMICOM_WRITE_TARGET umicom-writable-block-image)
            set(UMICOM_WRITE_OUTPUT umicom-writable-block)
            set(UMICOM_WRITE_DEFINITION UMICOM_KERNEL_WRITABLE_BLOCK_TEST=1)
        else()
            set(UMICOM_WRITE_TARGET umicom-block-write-readback-image)
            set(UMICOM_WRITE_OUTPUT umicom-block-write-readback)
            set(UMICOM_WRITE_DEFINITION UMICOM_KERNEL_BLOCK_WRITE_READBACK_TEST=1)
        endif()
        add_executable(${UMICOM_WRITE_TARGET} ${UMICOM_WRITE_SOURCES}
            "${CMAKE_CURRENT_SOURCE_DIR}/kernel/writable_block_boot.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/kernel/writable_block_validation.c")
        target_include_directories(${UMICOM_WRITE_TARGET} PRIVATE ${UMICOM_WRITE_INCLUDES})
        target_compile_options(${UMICOM_WRITE_TARGET} PRIVATE ${UMICOM_WRITE_OPTIONS})
        target_compile_definitions(${UMICOM_WRITE_TARGET} PRIVATE ${UMICOM_WRITE_DEFINITION})
        if(UMICOM_WRITE_DEFINITIONS)
            target_compile_definitions(${UMICOM_WRITE_TARGET} PRIVATE ${UMICOM_WRITE_DEFINITIONS})
        endif()
        if(UMICOM_WRITE_LIBRARIES)
            target_link_libraries(${UMICOM_WRITE_TARGET} PRIVATE ${UMICOM_WRITE_LIBRARIES})
        endif()
        target_link_options(${UMICOM_WRITE_TARGET} PRIVATE ${UMICOM_WRITE_LINK_OPTIONS}
            "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/${UMICOM_WRITE_OUTPUT}.map")
        set_target_properties(${UMICOM_WRITE_TARGET} PROPERTIES OUTPUT_NAME "${UMICOM_WRITE_OUTPUT}"
            SUFFIX ".elf" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tests"
            LINK_DEPENDS "${UMICOM_KERNEL_LINKER_SCRIPT}")
        if(UMICOM_WRITE_DEPENDENCIES)
            add_dependencies(${UMICOM_WRITE_TARGET} ${UMICOM_WRITE_DEPENDENCIES})
        endif()
    endforeach()
    # Publish reverse dependencies only after both image source snapshots exist.
    add_dependencies(umicom-kernel umicom-writable-block-image umicom-block-write-readback-image)
    if(NOT UMICOM_QEMU_RISCV64)
        return()
    endif()
    set(UMICOM_WRITE_FIXTURE "${CMAKE_CURRENT_BINARY_DIR}/fixtures/umicom-writable-block.raw")
    if(UMICOM_WRITE_FIXTURE MATCHES ",")
        message(FATAL_ERROR "The writable block test path must not contain a QEMU -drive separator comma.")
    endif()
    add_test(NAME kernel.riscv64.writable_block_prepare
        COMMAND "${CMAKE_COMMAND}" -E copy "${UMICOM_BLOCK_FIXTURE_SOURCE}" "${UMICOM_WRITE_FIXTURE}")
    set_tests_properties(kernel.riscv64.writable_block_prepare PROPERTIES TIMEOUT 10
        FIXTURES_REQUIRED kernel_current_image FIXTURES_SETUP kernel_writable_block_fixture)
    add_test(NAME kernel.riscv64.writable_block
        COMMAND "${UMICOM_QEMU_RISCV64}" -machine "virt,aclint=off"
            -bios "$<TARGET_FILE:umicom-writable-block-image>"
            -display none -monitor none -serial stdio -m 128M -smp 1 -no-reboot
            -global virtio-mmio.force-legacy=false
            -drive "file=${UMICOM_WRITE_FIXTURE},if=none,format=raw,id=umicom_write_test,readonly=off,cache=writeback,rerror=report,werror=report"
            -device "virtio-blk-device,drive=umicom_write_test,write-cache=on,logical_block_size=512,physical_block_size=512,num-queues=1")
    set_tests_properties(kernel.riscv64.writable_block PROPERTIES TIMEOUT 30
        FIXTURES_REQUIRED "kernel_current_image;kernel_writable_block_fixture"
        FIXTURES_SETUP kernel_writable_block_completed
        PASS_REGULAR_EXPRESSION "UMICOM_KERNEL_WRITABLE_BLOCK_READY"
        FAIL_REGULAR_EXPRESSION "UMICOM_KERNEL_FAIL;unexpected-trap;panic;PANIC;failed;FAILED")
    add_test(NAME kernel.riscv64.block_write_readback
        COMMAND "${UMICOM_QEMU_RISCV64}" -machine "virt,aclint=off"
            -bios "$<TARGET_FILE:umicom-block-write-readback-image>"
            -display none -monitor none -serial stdio -m 128M -smp 1 -no-reboot
            -global virtio-mmio.force-legacy=false
            -drive "file=${UMICOM_WRITE_FIXTURE},if=none,format=raw,id=umicom_write_readback,readonly=on,cache=writeback,rerror=report"
            -device "virtio-blk-device,drive=umicom_write_readback,logical_block_size=512,physical_block_size=512,num-queues=1")
    set_tests_properties(kernel.riscv64.block_write_readback PROPERTIES TIMEOUT 30
        FIXTURES_REQUIRED "kernel_current_image;kernel_writable_block_fixture;kernel_writable_block_completed"
        PASS_REGULAR_EXPRESSION "UMICOM_KERNEL_BLOCK_WRITE_READBACK_READY"
        FAIL_REGULAR_EXPRESSION "UMICOM_KERNEL_FAIL;unexpected-trap;panic;PANIC;failed;FAILED")
    add_test(NAME kernel.riscv64.writable_block_cleanup
        COMMAND "${CMAKE_COMMAND}" -E rm -f "${UMICOM_WRITE_FIXTURE}")
    # Cleanup needs no executable. It must also remove a stale disposable copy
    # when the current-image build fails and all guest stages are skipped.
    set_tests_properties(kernel.riscv64.writable_block_cleanup PROPERTIES TIMEOUT 10
        FIXTURES_CLEANUP kernel_writable_block_fixture)
endfunction()
cmake_language(DEFER CALL UmicomConfigureWritableBlockImages)
