#-----------------------------------------------------------------------------
# Umicom Kernel
# File: tests/cvm_evidence/check.cmake
#
# PURPOSE:
#   Exercise source and artifact identity refusal with non-executable fixtures.
#
# AUTHOR AND ORGANISATION:
# Sammy Hegab
# Umicom Foundation
#
# LICENCE:
# MIT
#---------------------------------------------------------------------------
foreach(required IN ITEMS CASE WORK EVIDENCE)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Missing evidence-test argument ${required}")
    endif()
endforeach()
# Each CTest case owns its own directory. No recursive cleanup or real executable
# input is used: these files are plain identity fixtures, never guest programs.
file(MAKE_DIRECTORY "${WORK}/runtime" "${WORK}/kernel")
set(runtime_files src/status.c src/vm.c src/bytecode.c src/comparison.c
    src/internal.h include/umicom_c/umicom_c.h include/umicom_c/comparison.h
    cmake/UmicomCRuntime.cmake)
set(kernel_files programs/cvm/main.c programs/cvm/entry.S
    programs/cvm/CMakeLists.txt programs/diagnostic/linker.ld cmake/CvmPrograms.cmake)
foreach(relative IN LISTS runtime_files)
    get_filename_component(parent "${WORK}/runtime/${relative}" DIRECTORY)
    file(MAKE_DIRECTORY "${parent}")
    file(WRITE "${WORK}/runtime/${relative}" "fixture runtime ${relative}\n")
endforeach()
foreach(relative IN LISTS kernel_files)
    get_filename_component(parent "${WORK}/kernel/${relative}" DIRECTORY)
    file(MAKE_DIRECTORY "${parent}")
    file(WRITE "${WORK}/kernel/${relative}" "fixture kernel ${relative}\n")
endforeach()
file(WRITE "${WORK}/o0.fixture" "not executable: O0")
file(WRITE "${WORK}/o2.fixture" "not executable: O2")
file(WRITE "${WORK}/recipe.txt" "fixture compiler recipe")
set(arguments "-DSOURCE=${WORK}/runtime" "-DKERNEL=${WORK}/kernel"
    "-DO0=${WORK}/o0.fixture" "-DO2=${WORK}/o2.fixture"
    "-DRECEIPT=${WORK}/receipt.txt" "-DRECIPE=${WORK}/recipe.txt")
execute_process(COMMAND "${CMAKE_COMMAND}" -DMODE=capture ${arguments} -P "${EVIDENCE}"
    RESULT_VARIABLE capture OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT "${capture}" STREQUAL "0")
    message(FATAL_ERROR "Fixture capture failed: ${out} ${err}")
endif()
set(mode verify)
set(expect_success FALSE)
if(CASE STREQUAL "unchanged")
    set(expect_success TRUE)
elseif(CASE STREQUAL "source-edit")
    file(APPEND "${WORK}/runtime/src/vm.c" "changed source")
elseif(CASE STREQUAL "image-edit")
    file(APPEND "${WORK}/o2.fixture" "changed image")
elseif(CASE STREQUAL "recipe-edit")
    file(APPEND "${WORK}/recipe.txt" "changed flags")
elseif(CASE STREQUAL "receipt-missing")
    file(REMOVE "${WORK}/receipt.txt")
elseif(CASE STREQUAL "source-missing")
    file(REMOVE "${WORK}/runtime/src/vm.c")
elseif(CASE STREQUAL "unknown-mode")
    set(mode unsupported)
else()
    message(FATAL_ERROR "Unknown evidence-test case ${CASE}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" "-DMODE=${mode}" ${arguments} -P "${EVIDENCE}"
    RESULT_VARIABLE verified OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(expect_success)
    if(NOT "${verified}" STREQUAL "0")
        message(FATAL_ERROR "Unchanged identity was refused: ${out} ${err}")
    endif()
elseif("${verified}" STREQUAL "0")
    message(FATAL_ERROR "Changed or missing evidence was accepted: ${CASE}")
endif()

# A failure for an unrelated reason must not masquerade as successful rejection.
if(NOT expect_success)
    if(CASE STREQUAL "source-missing")
        set(reason "Missing CVM source")
    elseif(CASE STREQUAL "receipt-missing")
        set(reason "build the CVM image before guest qualification")
    elseif(CASE STREQUAL "unknown-mode")
        set(reason "Unknown CVM evidence mode")
    else()
        set(reason "CVM source or image changed since the build receipt")
    endif()
    if(NOT "${err}" MATCHES "${reason}")
        message(FATAL_ERROR "Unexpected failure for ${CASE}: ${out} ${err}")
    endif()
endif()
