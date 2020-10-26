add_executable(asyncs-tests
    main.cpp
    asio-steady-timer-impl-tests.cpp
    notifier-tests.cpp
    chan-tests.cpp
    lockable-value-tests.cpp
    manageable-task-tests.cpp
)
target_link_libraries(asyncs-tests gtest nhope)

add_test(asyncs-tests asyncs-tests)
