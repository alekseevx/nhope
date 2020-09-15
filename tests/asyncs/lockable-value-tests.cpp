#include <gtest/gtest.h>

#include <chrono>
#include <map>
#include <string>
#include <thread>

#include <nhope/asyncs/lockable-value.h>

using namespace nhope::asyncs;
using namespace std::chrono_literals;
using LockableMap = LockableValue<std::map<std::string, int>>;

TEST(LockableValue, ReadWrite)
{
    LockableMap lockableMap;

    auto writter = std::thread([&lockableMap]() {
        int counter = 0;
        while (counter < 100) {
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
                if (i->second >= 100) {
                    break;
                }
            }
            std::this_thread::sleep_for(1ms);
        }
    });

    writter.join();
    reader.join();
}