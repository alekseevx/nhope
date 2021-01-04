#include <gtest/gtest.h>

#include <chrono>
#include <map>
#include <string>
#include <thread>

#include <nhope/asyncs/lockable-value.h>

using namespace nhope;
using namespace std::chrono_literals;
using LockableMap = LockableValue<std::map<std::string, int>>;

TEST(LockableValue, ReadWrite)   // NOLINT
{
    constexpr int MaxCounter = 100;

    LockableMap lockableMap;

    auto writter = std::thread([&lockableMap]() {
        int counter = 0;
        while (counter < MaxCounter) {
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
                if (i->second >= MaxCounter) {
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
    constexpr int StartValue = 42;
    constexpr int MaxValueForWritter = 80;
    constexpr int MaxValueForReader = 77;

    LockableValue<int> lockInt(StartValue);
    auto writter = std::thread([&lockInt]() {
        while (true) {
            std::this_thread::sleep_for(1ms);
            auto v = lockInt.copy();
            lockInt = ++v;
            if (v == MaxValueForWritter) {
                break;
            }
        }
    });

    auto reader = std::thread([&lockInt]() {
        while (true) {
            if (lockInt.copy() > MaxValueForReader) {
                break;
            }
            std::this_thread::sleep_for(1ms);
        }
    });

    reader.join();
    writter.join();

    ASSERT_EQ(lockInt.copy(), 80);
}