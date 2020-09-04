if(NOT ${BASTARD_DIR} STREQUAL "")
    include("${BASTARD_DIR}/bastard.cmake")
    return()
endif()

#---------------------------------------------------
# поиск пакетного менеджера
execute_process(
        COMMAND bastard version
        OUTPUT_VARIABLE _BASTARD_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE exit_code
    )

if(NOT exit_code EQUAL "0")
    message(FATAL_ERROR "Failed to run bastard")
endif()

message("Bastard version: ${_BASTARD_VERSION}")

#---------------------------------------------------
# получение каталога с исходниками
execute_process(
        COMMAND bastard location
        OUTPUT_VARIABLE _BASTARD_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE exit_code
    )

if(NOT exit_code EQUAL "0")
    message(FATAL_ERROR "Failed to run bastard")
endif()

message("Bastard location: ${_BASTARD_DIR}")

#---------------------------------------------------
# включение файла bastard_setup.cmake
set(BASTARD_DIR ${_BASTARD_DIR})
include("${BASTARD_DIR}/bastard.cmake")
#include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/bastard.cmake")
