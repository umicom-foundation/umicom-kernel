# Umicom Kernel checked primary partitions and read-only FAT16 inspection.
# The existing raw-sector fixture retains its own disk and acceptance test.
# Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
target_sources(umicom-kernel PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/disk_partitions.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/fat16_inspector.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/disk_console.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/disk_inspection_validation.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/disk_inspection_boot.c")
set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/kernel/console_shell.c" APPEND
    PROPERTY COMPILE_DEFINITIONS UMICOM_KERNEL_DISK_INSPECTION=1)

set(UMICOM_DISK_INSPECTION_FIXTURE_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests/disk_inspection/fixture.raw")
file(SHA256 "${UMICOM_DISK_INSPECTION_FIXTURE_SOURCE}" UMICOM_DISK_INSPECTION_HASH)
if(NOT UMICOM_DISK_INSPECTION_HASH STREQUAL "bd866d6ae337527f3a8b4609f31969d60185e025f8525726f573d905a2f50938")
    message(FATAL_ERROR "The synthetic partition/FAT16 fixture does not match its recorded SHA-256.")
endif()
set(UMICOM_DISK_INSPECTION_FIXTURE "${CMAKE_CURRENT_BINARY_DIR}/fixtures/umicom-fat16-read-only.raw")
configure_file("${UMICOM_DISK_INSPECTION_FIXTURE_SOURCE}" "${UMICOM_DISK_INSPECTION_FIXTURE}" COPYONLY)
if(UMICOM_DISK_INSPECTION_FIXTURE MATCHES ",")
    message(FATAL_ERROR "The disk fixture build path cannot contain a comma (QEMU -drive separator).")
endif()

function(UmicomConfigureDiskInspectionImage)
    if(NOT BUILD_TESTING)
        return()
    endif()
    # Deferred capture includes all existing source-local definitions and linked
    # ELF carriers. Capture dependencies before linking the new image back to
    # the ordinary build fixture, avoiding a dependency cycle.
    get_target_property(UMICOM_DISK_SOURCES umicom-kernel SOURCES)
    get_target_property(UMICOM_DISK_INCLUDES umicom-kernel INCLUDE_DIRECTORIES)
    get_target_property(UMICOM_DISK_OPTIONS umicom-kernel COMPILE_OPTIONS)
    get_target_property(UMICOM_DISK_LINK_OPTIONS umicom-kernel LINK_OPTIONS)
    get_target_property(UMICOM_DISK_DEFINITIONS umicom-kernel COMPILE_DEFINITIONS)
    get_target_property(UMICOM_DISK_LIBRARIES umicom-kernel LINK_LIBRARIES)
    get_target_property(UMICOM_DISK_DEPENDENCIES umicom-kernel MANUALLY_ADDED_DEPENDENCIES)
    add_executable(umicom-disk-inspection-image ${UMICOM_DISK_SOURCES})
    target_include_directories(umicom-disk-inspection-image PRIVATE ${UMICOM_DISK_INCLUDES})
    target_compile_options(umicom-disk-inspection-image PRIVATE ${UMICOM_DISK_OPTIONS})
    target_compile_definitions(umicom-disk-inspection-image PRIVATE UMICOM_KERNEL_DISK_INSPECTION_TEST=1)
    if(UMICOM_DISK_DEFINITIONS)
        target_compile_definitions(umicom-disk-inspection-image PRIVATE ${UMICOM_DISK_DEFINITIONS})
    endif()
    if(UMICOM_DISK_LIBRARIES)
        target_link_libraries(umicom-disk-inspection-image PRIVATE ${UMICOM_DISK_LIBRARIES})
    endif()
    target_link_options(umicom-disk-inspection-image PRIVATE ${UMICOM_DISK_LINK_OPTIONS}
        "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/umicom-disk-inspection.map")
    set_target_properties(umicom-disk-inspection-image PROPERTIES OUTPUT_NAME "umicom-disk-inspection"
        SUFFIX ".elf" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tests"
        LINK_DEPENDS "${UMICOM_KERNEL_LINKER_SCRIPT}")
    if(UMICOM_DISK_DEPENDENCIES)
        add_dependencies(umicom-disk-inspection-image ${UMICOM_DISK_DEPENDENCIES})
    endif()
    add_dependencies(umicom-kernel umicom-disk-inspection-image)
    if(UMICOM_QEMU_RISCV64)
        add_test(NAME kernel.riscv64.disk_inspection
            COMMAND "${UMICOM_QEMU_RISCV64}" -machine "virt,aclint=off"
                -bios "$<TARGET_FILE:umicom-disk-inspection-image>"
                -display none -monitor none -serial stdio -m 128M -smp 1 -no-reboot
                -global virtio-mmio.force-legacy=false
                -drive "file=${UMICOM_DISK_INSPECTION_FIXTURE},if=none,format=raw,id=umicom_fat_test,readonly=on"
                -device "virtio-blk-device,drive=umicom_fat_test")
        set_tests_properties(kernel.riscv64.disk_inspection PROPERTIES TIMEOUT 30
            FIXTURES_REQUIRED kernel_current_image
            PASS_REGULAR_EXPRESSION "UMICOM_KERNEL_DISK_INSPECTION_READY"
            FAIL_REGULAR_EXPRESSION "UMICOM_KERNEL_FAIL;unexpected-trap;panic;PANIC;failed;FAILED")
    endif()
endfunction()
cmake_language(DEFER CALL UmicomConfigureDiskInspectionImage)
