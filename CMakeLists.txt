cmake_minimum_required(VERSION 3.1)

include(cmake/enable_warnings.cmake)
# add_compile_options(-fsanitize=thread -fsanitize-blacklist=${CMAKE_CURRENT_LIST_DIR}/sanitize-blacklist)
# add_link_options(-fsanitize=thread -fsanitize-blacklist=${CMAKE_CURRENT_LIST_DIR}/sanitize-blacklist)

include(cmake/bastard_setup.cmake)
bastard_setup()
project(${BASTARD_PACKAGE_NAME})

target_compile_definitions(${BASTARD_PACKAGE_NAME}
    PUBLIC BOOST_THREAD_VERSION=5
)

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    add_subdirectory(tests)
endif()
