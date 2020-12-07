set(CMAKE_CXX_STANDARD 17)

add_executable(asyncs-tests
    main.cpp
    timer-tests.cpp
    notifier-tests.cpp
    chan-tests.cpp
    lockable-value-tests.cpp
    manageable-task-tests.cpp
    func-executor-tests.cpp
    async-invoke-tests.cpp
)
target_link_libraries(asyncs-tests gtest nhope)

add_test(asyncs-tests asyncs-tests)
