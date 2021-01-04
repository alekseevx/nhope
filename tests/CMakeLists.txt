set(CMAKE_CXX_STANDARD 17)

add_executable(nhope-tests
    main.cpp

    async/async-invoke-tests.cpp
    async/future-tests.cpp
    async/lockable-value-tests.cpp
    async/manageable-task-tests.cpp
    async/scheduler-tests.cpp
    async/timer-tests.cpp
    async/ts-queue-tests.cpp
    
    seq/chan-tests.cpp
    seq/notifier-tests.cpp

    utils/exception_ptr_tests.cpp
)
target_link_libraries(nhope-tests gtest nhope)

add_test(nhope-tests nhope-tests)
