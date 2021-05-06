#pragma once

#include <atomic>
#include <chrono>
#include <thread>

template<typename T>
bool waitForValue(std::chrono::nanoseconds timeout, std::atomic<T>& var, const T& value)
{
    using namespace std::chrono_literals;
    using clock = std::chrono::steady_clock;

    const auto time = clock::now() + timeout;
    while (var != value && clock::now() < time) {
        std::this_thread::sleep_for(1ms);
    }

    return var == value;
}

template<typename Pred>
bool waitForPred(std::chrono::nanoseconds timeout, Pred pred)
{
    using namespace std::chrono_literals;
    using clock = std::chrono::steady_clock;

    const auto time = clock::now() + timeout;
    while (!pred() && clock::now() <= time) {
        std::this_thread::sleep_for(1ms);
    }

    return pred();
}
