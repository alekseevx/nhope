#include <exception>
#include <stdexcept>
#include <type_traits>
#include <variant>
#include <iostream>

#include <gtest/gtest.h>

#include "nhope/async/future.h"
#include "nhope/async/state-observer.h"
#include "nhope/seq/consumer.h"
#include "nhope/async/thread-executor.h"

namespace {
using namespace std::literals;
using IntConsumer = nhope::ObservableState<int>;

class StateObserverTest final
{
    int m_value{};
    nhope::StateObserver<int> m_state;

public:
    explicit StateObserverTest(nhope::ThreadExecutor& e)
      : m_state(
          [this](auto&& v) {
              return setRemoteState(std::move(v));
          },
          [this] {
              return getRemoteState();
          },
          e)
    {}
    nhope::StateObserver<int>& observer()
    {
        return m_state;
    }

    nhope::Future<void> setRemoteState(int&& newVal)
    {
        m_value = newVal;
        return nhope::makeReadyFuture();
    }

    [[nodiscard]] nhope::Future<int> getRemoteState() const
    {
        int value = m_value;
        return nhope::makeReadyFuture<int>(value);
    }
};

class SimpleConsumer : public nhope::Consumer<IntConsumer>
{
    std::function<void(IntConsumer)> m_f;

public:
    SimpleConsumer(std::function<void(IntConsumer)>&& f)
      : m_f(f)
    {}
    nhope::Consumer<IntConsumer>::Status consume(const IntConsumer& value) final
    {
        m_f(value);
        return nhope::Consumer<IntConsumer>::Status::Ok;
    }
};

}   // namespace

TEST(StateObserver, SimpleObserver)   // NOLINT
{
    constexpr auto value{42};
    nhope::ThreadExecutor e;
    StateObserverTest observer(e);
    auto consumer = std::make_unique<SimpleConsumer>([value](auto v) {
        v.value([value](int val) {
             EXPECT_EQ(val, value);
         })
          .fail([](auto /*unused*/) {
              FAIL();
          });
    });
    observer.observer().attachConsumer(std::move(consumer));
    observer.observer().setState(value);
    EXPECT_EQ(observer.observer().getState().value(), value);
    std::this_thread::sleep_for(200ms);
}

TEST(StateObserver, ObserverState)   // NOLINT
{
    constexpr auto value{42};
    nhope::ObservableState<int> state(value);
    nhope::ObservableState<int> state2;
    EXPECT_TRUE(state.hasValue());
    EXPECT_TRUE(state2.hasException());
    EXPECT_THROW(std::rethrow_exception(state2.exception()), nhope::StateUninitialized);   //NOLINT

    EXPECT_NE(state, state2);
}

TEST(StateObserver, ObserverFailConstruct)   // NOLINT
{
    nhope::ThreadExecutor e;
    EXPECT_THROW(nhope::StateObserver<int> observer(nullptr, nullptr, e), nhope::StateUninitialized);   //NOLINT
}

TEST(StateObserver, Exception)   // NOLINT
{
    using namespace nhope;
    ThreadExecutor e;
    constexpr auto magic = 42;
    std::atomic_int value{};

    StateObserver<int> observer(
      [&](int&& newVal) {
          if (newVal == magic) {
              throw std::runtime_error("magic set");
          }
          value = newVal;
          return makeReadyFuture();
      },
      [&] {
          auto current = value.load();
          if (current == magic + 1) {
              value = 2;
              throw std::runtime_error("magic get");
          }
          if (current == magic + 2) {
              Promise<int> failPromise;
              failPromise.setException(std::make_exception_ptr(std::runtime_error("magic")));
              auto fail = failPromise.future();
              return fail;
          }
          return makeReadyFuture<int>(current);
      },
      e);   //NOLINT

    auto consumer = std::make_unique<SimpleConsumer>([&](auto v) {
        v.value([](int /*unused*/) {

         })
          .fail([&](auto except) {
              EXPECT_EQ(observer.getState(), except);
          });
    });
    observer.attachConsumer(std::move(consumer));

    std::this_thread::sleep_for(1s);
    observer.setState(magic);
    std::this_thread::sleep_for(500ms);
    observer.setState(magic + 1);
    std::this_thread::sleep_for(500ms);
    observer.setState(magic + 2);
    std::this_thread::sleep_for(500ms);
}