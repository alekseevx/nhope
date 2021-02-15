#pragma once

#include <complex>
#include <exception>
#include <type_traits>
#include <utility>
#include <chrono>
#include <variant>

#include "nhope/async/ao-context.h"
#include "nhope/async/async-invoke.h"
#include "nhope/async/future.h"
#include "nhope/async/thread-executor.h"
#include "nhope/async/timer.h"
#include "nhope/seq/produser.h"
#include "nhope/seq/consumer-list.h"

namespace nhope {
using namespace std::literals;

class StateUninitialized : public std::runtime_error
{
public:
    explicit StateUninitialized(std::string_view s)
      : std::runtime_error(s.data())
    {}
};

template<typename T>
class ObservableState final
{
    using ObservableStateV = std::variant<T, std::exception_ptr>;

    ObservableStateV m_state;

public:
    template<typename... Args>
    explicit ObservableState(Args... args)
      : m_state(std::forward<Args>(args)...)
    {}

    explicit ObservableState()
    {
        m_state = std::make_exception_ptr(StateUninitialized("state not initialized"));
    }

    template<typename V>
    bool operator==(V&& rhs) const
    {
        if constexpr (std::is_same_v<std::decay_t<V>, ObservableState<T>>) {
            return m_state == rhs.m_state;
        } else if constexpr (std::is_same_v<std::decay_t<V>, std::exception_ptr>) {
            return hasException() && std::get<1>(m_state) == rhs;
        } else {
            return hasValue() && std::get<0>(m_state) == rhs;
        }
    }

    template<typename V>
    bool operator!=(V&& rhs) const
    {
        return !(*this == rhs);
    }

    ObservableState& operator=(const T& value)
    {
        m_state = value;
        return *this;
    }

    template<typename V>
    ObservableState& operator=(V&& value)
    {
        if constexpr (std::is_same_v<std::decay_t<V>, ObservableState<T>>) {
            m_state = value.state;
        } else {
            m_state = value;
        }
        return *this;
    }

    template<typename Fn>
    ObservableState& value(Fn fn)
    {
        if (m_state.index() == 0) {
            fn(std::get<0>(m_state));
        }
        return *this;
    }

    template<typename Fn>
    ObservableState& fail(Fn fn)
    {
        if (m_state.index() == 1) {
            fn(std::get<1>(m_state));
        }
        return *this;
    }

    [[nodiscard]] bool hasValue() const
    {
        return m_state.index() == 0;
    }

    [[nodiscard]] bool hasException() const
    {
        return m_state.index() == 1;
    }

    [[nodiscard]] T value() const
    {
        return std::get<0>(m_state);
    }

    [[nodiscard]] std::exception_ptr exception() const
    {
        return std::get<1>(m_state);
    }
};

template<typename T>
class StateObserver : public nhope::Produser<ObservableState<T>>
{
public:
    static constexpr auto defaultPollTime = 100ms;

    explicit StateObserver(ThreadExecutor& executor, std::chrono::nanoseconds pollTime = defaultPollTime)
      : m_pollTime(pollTime)
      , m_executor(executor)
      , m_aoCtx(std::make_unique<AOContext>(executor))
    {
        setTimeout(*m_aoCtx, pollTime, [this](auto /*unused*/) {
            updateState();
        });
    }

    void attachConsumer(std::unique_ptr<nhope::Consumer<ObservableState<T>>> consumer) final
    {
        m_consumers.addConsumer(std::move(consumer));
    }

    StateObserver(const StateObserver&) = delete;
    StateObserver& operator=(const StateObserver&) = delete;

    ObservableState<T> getState()
    {
        std::scoped_lock lock(m_mutex);
        return m_state;
    }

    template<typename V>
    void setState(V&& v)
    {
        std::scoped_lock lock(m_mutex);
        T newVal = std::forward<V>(v);
        m_state = newVal;

        m_aoCtx = std::make_unique<AOContext>(m_executor);
        asyncInvoke(*m_aoCtx,
                    [this, newVal]() mutable {
                        setRemoteState(std::move(newVal));
                    })
          .fail(*m_aoCtx,
                [this](auto exception) {
                    m_state = exception;
                })
          .then(*m_aoCtx, [this] {
              updateState();
          });
    }

protected:
    virtual Future<void> setRemoteState(T&&) = 0;
    virtual Future<T> getRemoteState() = 0;

private:
    void updateState()
    {
        std::scoped_lock lock(m_mutex);
        getRemoteState()
          .then(*m_aoCtx,
                [this](T state) {
                    setNewState(state);
                })
          .fail(*m_aoCtx,
                [this](auto exception) {
                    setNewState(exception);
                })
          .then(*m_aoCtx, [this] {
              return setTimeout(*m_aoCtx, m_pollTime, [this](auto /*unused*/) {
                  updateState();
              });
          });
    }

    template<typename V>
    void setNewState(V&& newState)
    {
        if (m_state != newState) {
            m_state = newState;
            m_consumers.consume(m_state);
        }
    }

private:
    std::mutex m_mutex;
    std::chrono::nanoseconds m_pollTime;

    ObservableState<T> m_state;
    ConsumerList<ObservableState<T>> m_consumers;
    ThreadExecutor& m_executor;
    std::unique_ptr<AOContext> m_aoCtx;
};

}   // namespace nhope