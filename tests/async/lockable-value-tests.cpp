#include <gtest/gtest.h>

#include <chrono>
#include <map>
#include <string>
#include <thread>

#include <nhope/async/lockable-value.h>

using namespace nhope;
using namespace std::chrono_literals;
using LockableMap = LockableValue<std::map<std::string, int>>;

TEST(LockableValue, ReadWrite)   // NOLINT
{
    static constexpr int maxCounter = 100;

    LockableMap lockableMap;

    auto writter = std::thread([&lockableMap]() {
        int counter = 0;
        while (counter < maxCounter) {
            auto wa = lockableMap.writeAccess();
            (*wa)["counter"] = ++counter;
        }
        std::this_thread::sleep_for(5ms);
    });

    auto reader = std::thread([&lockableMap]() {
        while (true) {
            auto ra = lockableMap.readAccess();
            auto i = ra->find("counter");
            if (i != ra->end()) {
                if (i->second >= maxCounter) {
                    break;
                }
            }
            std::this_thread::sleep_for(1ms);
        }
    });

    writter.join();
    reader.join();
}

TEST(LockableValue, CopyValue)   // NOLINT
{
    static constexpr int startValue = 42;
    static constexpr int maxValueForWritter = 80;
    static constexpr int maxValueForReader = 77;

    LockableValue<int> lockInt(startValue);
    auto writter = std::thread([&lockInt]() {
        while (true) {
            std::this_thread::sleep_for(1ms);
            auto v = lockInt.copy();
            lockInt = ++v;
            if (v == maxValueForWritter) {
                break;
            }
        }
    });

    auto reader = std::thread([&lockInt]() {
        while (true) {
            if (lockInt.copy() > maxValueForReader) {
                break;
            }
            std::this_thread::sleep_for(1ms);
        }
    });

    reader.join();
    writter.join();

    ASSERT_EQ(lockInt.copy(), 80);
}