cmake_minimum_required(VERSION 3.11)

include(cmake/enable_warnings.cmake)
include(cmake/sanitizer.cmake)
include(cmake/bastard_setup.cmake)

option(COVERAGE_ENABLED "enable coverage compiler flags" OFF)
option(ADDRESS_SANITIZER_ENABLED "enable address sanitizer" OFF)
option(THREAD_SANITIZER_ENABLED "enable thread sanitizer" OFF)
option(MEMORY_SANITIZER_ENABLED "enable memory sanitizer" OFF)

if(THREAD_SANITIZER_ENABLED)        
    enable_thread_sanitizer(
        blacklist ${CMAKE_CURRENT_LIST_DIR}/sanitize-blacklist)
endif()

bastard_setup()
project(${BASTARD_PACKAGE_NAME})

if(COVERAGE_ENABLED)    
    message(STATUS "nhope enabling coverage")
    include(cmake/CodeCoverage.cmake)
    append_coverage_compiler_flags()
    setup_target_for_coverage_gcovr_html(
          NAME coverage
          EXECUTABLE ./tests
          EXCLUDE ".deps" "tests" "benchs"
          )          
endif()

if(ADDRESS_SANITIZER_ENABLED)
    enable_address_sanitizer(TARGET ${BASTARD_PACKAGE_NAME})
endif()

if(MEMORY_SANITIZER_ENABLED)        
    enable_memory_sanitizer(
        TARGET ${BASTARD_PACKAGE_NAME} 
        blacklist ${CMAKE_CURRENT_LIST_DIR}/sanitize-blacklist)
endif()

if (WIN32)
    target_compile_definitions(${BASTARD_PACKAGE_NAME} PUBLIC -D_WIN32_WINNT=0x0601)
endif()

if (NOT WIN32)
    target_include_directories(nhope PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/specific/linux)
    target_link_libraries(nhope -latomic)
endif()

EnableWarnings(${BASTARD_PACKAGE_NAME})

