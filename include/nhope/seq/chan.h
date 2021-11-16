#pragma once

#include <cstddef>

#include <atomic>
#include <limits>
#include <memory>
#include <optional>
#include <utility>

#include "nhope/async/ts-queue.h"

#include "producer.h"

namespace nhope {

template<typename T>
class Chan final
{
public:
    Chan(const Chan&) = delete;
    Chan& operator=(const Chan&) = delete;

    Chan(bool autoClose = true, std::size_t capacity = std::numeric_limits<std::size_t>::max())
      : m_d(std::make_shared<Prv>(autoClose, capacity))
    {}

    ~Chan()
    {
        this->close();
    }

    void close()
    {
        m_d->queue.close();
    }

    bool get(T& value)
    {
        return m_d->queue.read(value);
    }

    std::optional<T> get()
    {
        return m_d->queue.read();
    }

    void attachToProducer(Producer<T>& producer)
    {
        auto newInput = this->makeInput();
        producer.attachConsumer(std::move(newInput));
    }

    std::unique_ptr<Consumer<T>> makeInput()
    {
        return std::make_unique<Input>(m_d);
    }

private:
    struct Prv
    {
        Prv(bool autoClose, std::size_t capacity)
          : autoClose(autoClose)
          , queue(capacity)
        {}

        const bool autoClose;
        TSQueue<T> queue;
        std::atomic<std::size_t> inputCount = 0;
    };

    class Input final : public Consumer<T>
    {
    public:
        explicit Input(std::shared_ptr<Prv> d)
          : m_d(d)
        {
            ++m_d->inputCount;
        }

        ~Input() override
        {
            if (--m_d->inputCount == 0) {
                if (m_d->autoClose) {
                    m_d->queue.close();
                }
            }
        }

        typename Consumer<T>::Status consume(const T& value) override
        {
            if (m_d->queue.write(T(value))) {
                return Consumer<T>::Status::Ok;
            }
            return Consumer<T>::Status::Closed;
        }

    private:
        std::shared_ptr<Prv> m_d;
    };

    std::shared_ptr<Prv> m_d;
};

}   // namespace nhope
