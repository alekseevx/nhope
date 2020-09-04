# include_directories(${gtest_SOURCE_DIR}/include ${gtest_SOURCE_DIR})

add_executable(asyncs-tests
    main.cpp
    timeout-tests.cpp
)
target_link_libraries(asyncs-tests gtest nhope)

add_test(asyncs-tests asyncs-tests)
