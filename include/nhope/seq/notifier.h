#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <utility>

#include "consumer.h"
#include "produser.h"

namespace nhope {

template<typename T, typename Executor>
class Notifier final
{
public:
    using Handler = std::function<void(const T&)>;

public:
    Notifier(const Notifier&) = delete;
    Notifier& operator=(const Notifier&) = delete;

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
        return std::make_unique<Input>(m_d);
    }

    void close()
    {
        std::atomic_store(&m_d->closed, true);
        while (std::atomic_load(&m_d->useExecutorCounter) > 0) {
            ;
        }
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
            std::atomic_fetch_add(&m_d->useExecutorCounter, 1);
            if (std::atomic_load(&m_d->closed) == true) {
                std::atomic_fetch_sub(&m_d->useExecutorCounter, 1);
                return Consumer<T>::Status::Closed;
            }

            try {
                m_d->executor.post([d = m_d, value = T(value)]() {
                    if (!d->closed) {
                        d->handler(value);
                    }
                });
            } catch (...) {
            }
            std::atomic_fetch_sub(&m_d->useExecutorCounter, 1);

            return Consumer<T>::Status::Ok;
        }

    private:
        std::shared_ptr<Prv> m_d;
    };

private:
    std::shared_ptr<Prv> m_d;
};

}   // namespace nhope