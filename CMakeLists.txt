cmake_minimum_required(VERSION 3.11)

include(cmake/enable_warnings.cmake)
# add_compile_options(-fsanitize=thread -fsanitize-blacklist=${CMAKE_CURRENT_LIST_DIR}/sanitize-blacklist)
# add_link_options(-fsanitize=thread -fsanitize-blacklist=${CMAKE_CURRENT_LIST_DIR}/sanitize-blacklist)


include(cmake/bastard_setup.cmake)
bastard_setup()
project(${BASTARD_PACKAGE_NAME})

if (WIN32)
    target_compile_definitions(${BASTARD_PACKAGE_NAME} PUBLIC -D_WIN32_WINNT=0x0601)
endif()

EnableWarnings(${BASTARD_PACKAGE_NAME})
