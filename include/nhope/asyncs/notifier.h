#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <utility>

#include "consumer.h"
#include "produser.h"

namespace nhope::asyncs {

template<typename T, typename Executor>
class Notifier final
{
    Notifier(const Notifier&) = delete;
    Notifier& operator=(const Notifier&) = delete;

public:
    using Handler = std::function<void(const T&)>;

public:
    Notifier(Executor& executor, Handler&& handler)
    {
        m_d = std::make_shared<Prv>(executor, std::move(handler));
    }

    ~Notifier()
    {
        this->close();
    }

    void attachToProduser(Produser<T>& produser)
    {
        auto newInput = this->makeInput();
        produser.attachConsumer(std::move(newInput));
    }

    std::unique_ptr<Consumer<T>> makeInput()
    {
        return std::unique_ptr<Consumer<T>>(new Input(m_d));
    }

    void close()
    {
        m_d->closed = true;
        while (m_d->useExecutorCounter > 0)
            ;
    }

private:
    struct Prv
    {
        Prv(Executor& executor, Handler&& handler)
          : handler(std::move(handler))
          , executor(executor)
        {}

        Handler handler;
        Executor& executor;

        std::atomic<bool> closed = false;
        std::atomic<int> useExecutorCounter = 0;
    };

    class Input final : public Consumer<T>
    {
    public:
        explicit Input(std::shared_ptr<Prv> d)
          : m_d(d)
        {}

        typename Consumer<T>::Status consume(const T& value) override
        {
            if (m_d->closed) {
                return Consumer<T>::Status::Closed;
            }

            ++m_d->useExecutorCounter;
            try {
                m_d->executor.post([d = m_d, value = T(value)]() {
                    if (!d->closed) {
                        d->handler(value);
                    }
                });
            } catch (...) {
            }
            --m_d->useExecutorCounter;

            return Consumer<T>::Status::Ok;
        }

        std::shared_ptr<Prv> m_d;
    };

private:
    std::shared_ptr<Prv> m_d;
};

}   // namespace nhope::asyncs