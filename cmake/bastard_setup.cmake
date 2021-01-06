#---------------------------------------------------
# Базовый setup файл пакетного менеджера bastard
# Наличие этого файла - минимальное и достаточное требование для поддержки bastard
#---------------------------------------------------

# если пакетный менеджер уже был установлен
# P.S. должно выполняться для всех подзависимостей
if(NOT ${BASTARD_DIR} STREQUAL "")
    include("${BASTARD_DIR}/bastard.cmake")
    return()
endif()

set(BASTARD_SETUP_VERSION "0.3.0")
set(BASTARD_GIT_URI "git@gitlab.olimp.lan:cmake/bastard.git")
set(BASTARD_CACHE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.deps/bastard")

#---------------------------------------------------
# if the bastard was cached
if (EXISTS "${BASTARD_CACHE_DIR}/cmake/bastard.cmake")
    message("Using bastard from cache: ${BASTARD_CACHE_DIR}")
    set(BASTARD_DIR "${BASTARD_CACHE_DIR}/cmake")
    include("${BASTARD_DIR}/bastard.cmake")
    message("Bastard version: ${BASTARD_VERSION}")
    return()
endif()

#---------------------------------------------------
# load bastard from server
message("Trying to load bastard from server...")
message("Bastard uri: ${BASTARD_GIT_URI}")
execute_process(
    COMMAND bash -c "git clone --depth 1 ${BASTARD_GIT_URI} ${BASTARD_CACHE_DIR}"
    RESULT_VARIABLE exit_code
)
if(exit_code EQUAL "0")
    message("The bastard has been loaded: ${BASTARD_CACHE_DIR}")
    set(BASTARD_DIR "${BASTARD_CACHE_DIR}/cmake")
else()
    message(FATAL_ERROR "Failed to load bastard from server")
endif()

#---------------------------------------------------
# include bastard.cmake
include("${BASTARD_DIR}/bastard.cmake")
message("Bastard version: ${BASTARD_VERSION}")
