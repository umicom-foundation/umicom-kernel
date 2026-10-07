# Umicom Kernel read-only filesystem provider with explicit mount ownership.
# Existing raw-sector and partition-inspection tests keep their own images.
# Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
target_sources(umicom-kernel PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/fat16_provider.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/disk_filesystem.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/disk_filesystem_console.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/disk_filesystem_validation.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/disk_filesystem_boot.c")
set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/kernel/console_shell.c" APPEND
    PROPERTY COMPILE_DEFINITIONS UMICOM_KERNEL_DISK_FILESYSTEM=1)

function(UmicomConfigureDiskFilesystemImage)
    if(NOT BUILD_TESTING)
        return()
    endif()
    # Capture all completed source contributions before publishing a reverse
    # build dependency. This image cannot depend on itself via the main target.
    get_target_property(UMICOM_FILESYSTEM_SOURCES umicom-kernel SOURCES)
    get_target_property(UMICOM_FILESYSTEM_INCLUDES umicom-kernel INCLUDE_DIRECTORIES)
    get_target_property(UMICOM_FILESYSTEM_OPTIONS umicom-kernel COMPILE_OPTIONS)
    get_target_property(UMICOM_FILESYSTEM_LINK_OPTIONS umicom-kernel LINK_OPTIONS)
    get_target_property(UMICOM_FILESYSTEM_DEFINITIONS umicom-kernel COMPILE_DEFINITIONS)
    get_target_property(UMICOM_FILESYSTEM_LIBRARIES umicom-kernel LINK_LIBRARIES)
    get_target_property(UMICOM_FILESYSTEM_DEPENDENCIES umicom-kernel MANUALLY_ADDED_DEPENDENCIES)
    add_executable(umicom-disk-filesystem-image ${UMICOM_FILESYSTEM_SOURCES})
    target_include_directories(umicom-disk-filesystem-image PRIVATE ${UMICOM_FILESYSTEM_INCLUDES})
    target_compile_options(umicom-disk-filesystem-image PRIVATE ${UMICOM_FILESYSTEM_OPTIONS})
    target_compile_definitions(umicom-disk-filesystem-image PRIVATE UMICOM_KERNEL_DISK_FILESYSTEM_TEST=1)
    if(UMICOM_FILESYSTEM_DEFINITIONS)
        target_compile_definitions(umicom-disk-filesystem-image PRIVATE ${UMICOM_FILESYSTEM_DEFINITIONS})
    endif()
    if(UMICOM_FILESYSTEM_LIBRARIES)
        target_link_libraries(umicom-disk-filesystem-image PRIVATE ${UMICOM_FILESYSTEM_LIBRARIES})
    endif()
    target_link_options(umicom-disk-filesystem-image PRIVATE ${UMICOM_FILESYSTEM_LINK_OPTIONS}
        "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/umicom-disk-filesystem.map")
    set_target_properties(umicom-disk-filesystem-image PROPERTIES OUTPUT_NAME "umicom-disk-filesystem"
        SUFFIX ".elf" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tests"
        LINK_DEPENDS "${UMICOM_KERNEL_LINKER_SCRIPT}")
    if(UMICOM_FILESYSTEM_DEPENDENCIES)
        add_dependencies(umicom-disk-filesystem-image ${UMICOM_FILESYSTEM_DEPENDENCIES})
    endif()
    add_dependencies(umicom-kernel umicom-disk-filesystem-image)
    if(UMICOM_QEMU_RISCV64)
        add_test(NAME kernel.riscv64.disk_filesystem
            COMMAND "${UMICOM_QEMU_RISCV64}" -machine "virt,aclint=off"
                -bios "$<TARGET_FILE:umicom-disk-filesystem-image>"
                -display none -monitor none -serial stdio -m 128M -smp 1 -no-reboot
                -global virtio-mmio.force-legacy=false
                -drive "file=${UMICOM_DISK_INSPECTION_FIXTURE},if=none,format=raw,id=umicom_filesystem_test,readonly=on"
                -device "virtio-blk-device,drive=umicom_filesystem_test")
        set_tests_properties(kernel.riscv64.disk_filesystem PROPERTIES TIMEOUT 30
            FIXTURES_REQUIRED kernel_current_image
            PASS_REGULAR_EXPRESSION "UMICOM_KERNEL_DISK_FILESYSTEM_READY"
            FAIL_REGULAR_EXPRESSION "UMICOM_KERNEL_FAIL;unexpected-trap;panic;PANIC;failed;FAILED")
    endif()
endfunction()
cmake_language(DEFER CALL UmicomConfigureDiskFilesystemImage)
