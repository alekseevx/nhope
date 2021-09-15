#pragma once

#include <functional>
#include <memory>
#include <utility>

#include <nhope/async/ao-context.h>
#include <nhope/async/executor.h>
#include <nhope/async/safe-callback.h>
#include <nhope/seq/consumer.h>
#include <nhope/seq/producer.h>

namespace nhope {

template<typename T>
class Notifier final
{
public:
    using Handler = std::function<void(const T&)>;

    Notifier(const Notifier&) = delete;
    Notifier& operator=(const Notifier&) = delete;

    Notifier(AOContext& parentAOCtx, Handler handler)
      : m_handler(std::move(handler))
      , m_aoCtx(parentAOCtx)
    {}

    ~Notifier()
    {
        m_aoCtx.close();
    }

    void attachToProducer(Producer<T>& producer)
    {
        auto newInput = this->makeInput();
        producer.attachConsumer(std::move(newInput));
    }

    std::unique_ptr<Consumer<T>> makeInput()
    {
        auto safeHandler = makeSafeCallback(m_aoCtx, m_handler);
        return std::make_unique<Input>(std::move(safeHandler));
    }

private:
    class Input final : public Consumer<T>
    {
    public:
        explicit Input(Handler safeHandler)
          : m_safeHandler(std::move(safeHandler))
        {}

        typename Consumer<T>::Status consume(const T& value) override
        {
            try {
                m_safeHandler(value);
                return Consumer<T>::Status::Ok;
            } catch (const AOContextClosed&) {
                return Consumer<T>::Status::Closed;
            }
        }

    private:
        Handler m_safeHandler;
    };

    Handler m_handler;
    AOContext m_aoCtx;
};

}   // namespace nhope