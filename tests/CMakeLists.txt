set(CMAKE_CXX_STANDARD 17)

add_executable(nhope-tests
    main.cpp

    asyncs/async-invoke-tests.cpp
    asyncs/chan-tests.cpp
    asyncs/future-tests.cpp
    asyncs/lockable-value-tests.cpp
    asyncs/manageable-task-tests.cpp
    asyncs/scheduler-tests.cpp
    asyncs/notifier-tests.cpp
    asyncs/timer-tests.cpp
    asyncs/ts-queue-tests.cpp
    asyncs/take-tests.cpp

    utils/exception_ptr_tests.cpp
)
target_link_libraries(nhope-tests gtest nhope)

add_test(nhope-tests nhope-tests)
