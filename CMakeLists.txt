cmake_minimum_required(VERSION 3.1)

enable_language(CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/bastard_setup.cmake)
include(cmake/enable_warnings.cmake)

bastard_setup()

find_package(Boost REQUIRED COMPONENTS system thread)

project(${BASTARD_PACKAGE_NAME})
target_link_libraries(${BASTARD_PACKAGE_NAME} Boost::system Boost::thread)

add_subdirectory(tests)
