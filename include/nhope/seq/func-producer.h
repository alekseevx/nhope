#pragma once

#include <cassert>

#include <functional>
#include <memory>
#include <thread>
#include <utility>
#include <atomic>

#include "consumer-list.h"
#include "producer.h"

namespace nhope {

template<typename T>
class FuncProducer final : public Producer<T>
{
public:
    using Function = std::function<bool(T&)>;
    enum class State
    {
        ReadyToStart,
        Running,
        Stopping,
        Finished
    };

    explicit FuncProducer(Function&& func)
      : m_func(std::move(func))
      , m_state(State::ReadyToStart)
    {
        assert(m_func);
    }

    ~FuncProducer() override
    {
        this->stop();
        this->wait();
    }

    bool start()
    {
        State expected = State::ReadyToStart;
        if (!m_state.compare_exchange_strong(expected, State::Running)) {
            return false;
        }

        m_workThread = std::thread([this]() {
            this->run();
        });
        return true;
    }

    void stop()
    {
        State expected = State::Running;
        m_state.compare_exchange_strong(expected, State::Stopping);
    }

    void wait()
    {
        if (m_workThread.joinable()) {
            m_workThread.join();
        }
    }

    // Producer
    void attachConsumer(std::unique_ptr<Consumer<T>> consumer) override
    {
        m_consumerList.addConsumer(std::move(consumer));
    }

private:
    void run()
    {
        T value;
        while (m_state == State::Running && m_func(value)) {
            m_consumerList.consume(value);
        }

        m_consumerList.close();
    }

    std::thread m_workThread;
    const Function m_func;
    std::atomic<State> m_state;
    ConsumerList<T> m_consumerList;
};

}   // namespace nhope
