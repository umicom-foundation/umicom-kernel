#-----------------------------------------------------------------------------
# Umicom Kernel
# File: cmake/PrepareCvmHostArtifact.cmake
#
# PURPOSE:
#   Compile a small diagnostic artifact through an explicitly selected host
#   compiler while preserving the last complete output on build failure.
#
# AUTHOR AND ORGANISATION:
# Sammy Hegab
# Umicom Foundation
#
# LICENCE:
# MIT
#-----------------------------------------------------------------------------
foreach(_key IN ITEMS COMPILER INPUT OUTPUT)
    if(NOT DEFINED ${_key} OR NOT IS_ABSOLUTE "${${_key}}")
        message(FATAL_ERROR "The host artifact requires an absolute ${_key} path")
    endif()
endforeach()
get_filename_component(_directory "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${_directory}")
file(SHA256 "${COMPILER}" _compiler_before)
file(SHA256 "${INPUT}" _source_before)
string(RANDOM LENGTH 16 ALPHABET "0123456789abcdef" _nonce)
set(_candidate "${_directory}/host-artifact-${_nonce}.umcb")
execute_process(COMMAND "${COMPILER}" compile "${INPUT}" "${_candidate}"
    RESULT_VARIABLE _status OUTPUT_VARIABLE _report ERROR_VARIABLE _error TIMEOUT 15)
if(NOT "${_status}" STREQUAL "0" OR NOT EXISTS "${_candidate}")
    message(FATAL_ERROR "The host compiler did not publish a complete artifact (${_status})\n${_report}\n${_error}")
endif()
file(SIZE "${_candidate}" _size)
if(_size LESS 16 OR _size GREATER 1024)
    message(FATAL_ERROR "The diagnostic artifact is outside its expected byte budget")
endif()
file(SHA256 "${COMPILER}" _compiler_after)
file(SHA256 "${INPUT}" _source_after)
if(NOT _compiler_before STREQUAL _compiler_after OR NOT _source_before STREQUAL _source_after)
    message(FATAL_ERROR "The compiler or fixture changed during artifact preparation")
endif()
file(SHA256 "${_candidate}" _artifact)
set(_receipt "${_candidate}.receipt")
file(WRITE "${_receipt}" "compiler=${_compiler_after}\nsource=${_source_after}\nartifact=${_artifact}\n")
file(RENAME "${_candidate}" "${OUTPUT}")
file(RENAME "${_receipt}" "${OUTPUT}.receipt")
