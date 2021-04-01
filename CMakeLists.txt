cmake_minimum_required(VERSION 3.11)

include(cmake/enable_warnings.cmake)
# add_compile_options(-fsanitize=thread -fsanitize-blacklist=${CMAKE_CURRENT_LIST_DIR}/sanitize-blacklist)
# add_link_options(-fsanitize=thread -fsanitize-blacklist=${CMAKE_CURRENT_LIST_DIR}/sanitize-blacklist)


include(cmake/bastard_setup.cmake)
bastard_setup()
project(${BASTARD_PACKAGE_NAME})

option(COVERAGE_ENABLED "enable coverage compiler flags" OFF)

if(COVERAGE_ENABLED)    
    message(STATUS "nhope enabling coverage")
    include(cmake/CodeCoverage.cmake)
    append_coverage_compiler_flags()
    setup_target_for_coverage_gcovr_html(
          NAME coverage
          EXECUTABLE ./tests
          EXCLUDE ".deps" "tests"
          )
          
endif()


if (WIN32)
    target_compile_definitions(${BASTARD_PACKAGE_NAME} PUBLIC -D_WIN32_WINNT=0x0601)
endif()

EnableWarnings(${BASTARD_PACKAGE_NAME})
