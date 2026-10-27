# Umicom Kernel | Sammy Hegab, Umicom Foundation | MIT
# Link the actual boot code without a hosted runtime at debug and optimised
# settings. Hosted unit tests cannot detect compiler-inserted libc calls.
if(BUILD_TESTING)
    get_target_property(_boot_contract_options umicom-kernel COMPILE_OPTIONS)
    foreach(_boot_mode IN ITEMS debug optimised)
        set(_boot_target "umicom-boot-memory-${_boot_mode}-link")
        add_executable(${_boot_target}
            tests/device_tree/freestanding_link.c
            kernel/address.c kernel/boot_reservations.c
            kernel/device_tree_layout.c kernel/device_tree_reader.c)
        target_include_directories(${_boot_target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
        target_compile_options(${_boot_target} PRIVATE ${_boot_contract_options})
        if(_boot_mode STREQUAL "debug")
            target_compile_options(${_boot_target} PRIVATE -O0)
        else()
            target_compile_options(${_boot_target} PRIVATE -O2)
        endif()
        # No --gc-sections: even currently uncalled exported operations must
        # remain independent of a hosted C library. These are link-only images.
        target_link_options(${_boot_target} PRIVATE
            -march=rv64imac_zicsr -mabi=lp64 -mcmodel=medany -nostdlib -fuse-ld=lld
            -Wl,--entry=UmicomBootMemoryLinkCheck -Wl,--no-undefined -Wl,--no-relax)
        set_target_properties(${_boot_target} PROPERTIES SUFFIX ".elf"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/contracts")
    endforeach()
    add_custom_target(umicom-boot-memory-contracts
        DEPENDS umicom-boot-memory-debug-link umicom-boot-memory-optimised-link)
endif()
