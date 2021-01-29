#pragma once

#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <chrono>

#include <nhope/async/future.h>
#include <nhope/async/reverse_lock.h>
#include <nhope/seq/produser.h>

namespace nhope {
using namespace std::string_view_literals;

// Потокобезопасное свойство с возможностью отложенной установки
// Свойство будет применено только после вызова метода applyNewValue
template<typename T>
class DelayedProperty final
{
public:
    explicit DelayedProperty(const T& value) noexcept(std::is_nothrow_copy_assignable_v<T>)
    {
        m_d->currentValue = value;
    }

    explicit DelayedProperty(T&& value) noexcept(std::is_nothrow_move_assignable_v<T>)
    {
        m_d->currentValue = std::move(value);
    }

    template<typename... Args>
    explicit DelayedProperty(Args&&... args)
      : m_d(std::make_shared<Prv>())
    {
        m_d->currentValue = {std::forward<Args>(args)...};
    }

    DelayedProperty(const DelayedProperty&) = delete;
    DelayedProperty& operator=(const DelayedProperty&) = delete;

    ~DelayedProperty()
    {
        m_d->closed.store(true);
    }

    template<typename V>
    Future<void> setNewValue(V&& value)
    {
        return m_d->setNewValue(std::forward<V>(value));
    }

    [[nodiscard]] bool hasNewValue() const noexcept
    {
        return m_d->hasNewValue();
    }

    void waitNewValue()
    {
        m_d->waitNewValue();
    }

    bool waitNewValue(std::chrono::milliseconds timeout)
    {
        return m_d->waitNewValue(timeout);
    }

    template<typename Fn>
    void applyNewValue(Fn applyHandler)
    {
        m_d->applyNewValue(applyHandler);
    }

    T getCurrentValue() const
    {
        return m_d->getCurrentValue();
    }

    void attachToProduser(Produser<T>& produser)
    {
        produser.attachConsumer(this->makeInput());
    }

    std::unique_ptr<Consumer<T>> makeInput()
    {
        return std::make_unique<PropertyInput>(m_d);
    }

private:
    struct Prv
    {
        T currentValue;
        std::optional<T> newValue{std::nullopt};
        std::optional<Promise<void>> promiseOpt{std::nullopt};
        mutable std::mutex mutex;
        std::condition_variable newValCw;
        std::atomic_bool closed{false};

        template<typename V>
        Future<void> setNewValue(V&& value)
        {
            std::scoped_lock lock(mutex);

            if (currentValue == value) {
                return makeReadyFuture();
            }

            if (promiseOpt.has_value()) {
                auto ex = std::make_exception_ptr(AsyncOperationWasCancelled("previous value was ignored"sv));
                promiseOpt.value().setException(ex);
            }

            promiseOpt = Promise<void>();
            newValue = std::forward<V>(value);

            newValCw.notify_all();

            return promiseOpt.value().future();
        }

        [[nodiscard]] bool hasNewValue() const noexcept
        {
            std::scoped_lock lock(mutex);
            return promiseOpt.has_value();
        }

        T getCurrentValue() const
        {
            std::scoped_lock lock(mutex);
            return currentValue;
        }

        template<typename Fn>
        void applyNewValue(Fn applyHandler)
        {
            std::unique_lock lock(mutex);
            if (!promiseOpt.has_value()) {
                return;
            }
            assert(promiseOpt.has_value());   // NOLINT

            auto newVal = newValue.value();
            auto promise = std::move(promiseOpt.value());
            newValue = std::nullopt;
            promiseOpt = std::nullopt;

            try {
                {
                    ReverseLock unlock(lock);
                    applyHandler(newVal);
                }
                currentValue = newVal;
                promise.setValue();
            } catch (...) {
                promise.setException(std::current_exception());
            }
        }

        void waitNewValue()
        {
            std::unique_lock lock(mutex);
            newValCw.wait(lock, [this] {
                return promiseOpt.has_value();
            });
        }

        bool waitNewValue(std::chrono::milliseconds timeout)
        {
            std::unique_lock lock(mutex);
            return newValCw.wait_for(lock, timeout, [this] {
                return promiseOpt.has_value();
            });
        }
    };

    std::shared_ptr<Prv> m_d{std::make_shared<Prv>()};

    class PropertyInput final : public Consumer<T>
    {
        std::shared_ptr<Prv> m_d;

    public:
        PropertyInput(std::shared_ptr<Prv> d)
          : m_d(d)
        {}

        typename Consumer<T>::Status consume(const T& value) override
        {
            if (m_d->closed.load()) {
                return Consumer<T>::Status::Closed;
            }
            m_d->setNewValue(value);
            return Consumer<T>::Status::Ok;
        };
    };
};

}   // namespace nhope