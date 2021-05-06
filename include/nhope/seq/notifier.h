#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <utility>

#include <nhope/async/executor.h>
#include <nhope/seq/consumer.h>
#include <nhope/seq/produser.h>

namespace nhope {

template<typename T>
class Notifier final
{
public:
    using Handler = std::function<void(const T&)>;

    Notifier(const Notifier&) = delete;
    Notifier& operator=(const Notifier&) = delete;

    Notifier(SequenceExecutor& executor, Handler&& handler)
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
        Prv(SequenceExecutor& executor, Handler&& handler)
          : handler(std::move(handler))
          , executor(executor)
        {}

        Handler handler;
        SequenceExecutor& executor;

        std::atomic<bool> closed = false;
        std::atomic<int> useExecutorCounter = 0;
    };

    class Input final : public Consumer<T>
    {
    public:
        explicit Input(std::shared_ptr<Prv> d)
          : m_d(std::move(d))
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
                // FIXME: Handling an exception
            }
            std::atomic_fetch_sub(&m_d->useExecutorCounter, 1);

            return Consumer<T>::Status::Ok;
        }

    private:
        std::shared_ptr<Prv> m_d;
    };

    std::shared_ptr<Prv> m_d;
};

}   // namespace nhope