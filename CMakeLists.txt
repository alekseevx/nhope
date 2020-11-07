cmake_minimum_required(VERSION 3.1)
include(cmake/bastard_setup.cmake)
include(cmake/enable_warnings.cmake)
bastard_setup()
project(${BASTARD_PACKAGE_NAME})

target_compile_definitions(${BASTARD_PACKAGE_NAME}
    PUBLIC
        BOOST_THREAD_VERSION=5
        BOOST_THREAD_PROVIDES_FUTURE
        BOOST_THREAD_PROVIDES_EXECUTORS
)

add_subdirectory(tests)
