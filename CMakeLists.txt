cmake_minimum_required(VERSION 3.1)
project(${BASTARD_PACKAGE_NAME} LANGUAGE CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/bastard_setup.cmake)
include(cmake/enable_warnings.cmake)

bastard_setup()

find_package(Boost
    REQUIRED COMPONENTS
        chrono
        system
        thread
)

target_link_libraries(${BASTARD_PACKAGE_NAME}
    Boost::chrono
    Boost::system
    Boost::thread
)
target_compile_definitions(${BASTARD_PACKAGE_NAME}
    PUBLIC
        BOOST_THREAD_VERSION=5
        BOOST_THREAD_PROVIDES_FUTURE
        BOOST_THREAD_PROVIDES_EXECUTORS
)

add_subdirectory(tests)
