#include <exception>
#include <ostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
#include <iostream>

#include <gtest/gtest.h>

#include "fmt/core.h"
#include "nhope/async/ao-context.h"
#include "nhope/async/future.h"
#include "nhope/async/state-observer.h"
#include "nhope/async/thread-pool-executor.h"
#include "nhope/seq/chan.h"
#include "nhope/seq/consumer.h"

namespace {
using namespace std::literals;

class StateObserverTest final
{
public:
    static constexpr int startValue = 1000;

    explicit StateObserverTest()
      : m_state(
          [this](auto v) {
              return setRemoteState(v);
          },
          [this] {
              return getRemoteState();
          },
          nhope::ThreadPoolExecutor::defaultExecutor())
    {}
    nhope::StateObserver<int>& observer()
    {
        return m_state;
    }

    nhope::Future<void> setRemoteState(int newVal)
    {
        m_value = newVal;
        return nhope::makeReadyFuture();
    }

    nhope::Future<int> getRemoteState()
    {
        return nhope::makeReadyFuture<int>(m_value++);
    }

private:
    int m_value = startValue;
    nhope::StateObserver<int> m_state;
};

}   // namespace

namespace nhope {
template<typename T>
void PrintTo(const ObservableState<T>& state, std::ostream* os)   // NOLINT(readability-identifier-naming)
{
    if (state.hasException()) {
        try {
            std::rethrow_exception(state.exception());
        } catch (const std::exception& ex) {
            *os << fmt::format("Exception: {}", ex.what());
        }
        return;
    }

    *os << fmt::format("{}", state.value());
}
}   // namespace nhope

TEST(StateObserver, SimpleObserver)   // NOLINT
{
    using namespace nhope;
    StateObserverTest observerTest;

    {
        nhope::Chan<ObservableState<int>> stateChan;
        stateChan.attachToProduser(observerTest.observer());

        const int val = stateChan.get()->value();
        EXPECT_GE(val, StateObserverTest::startValue);
        EXPECT_EQ(stateChan.get()->value(), val + 1);
        EXPECT_EQ(stateChan.get()->value(), val + 2);
        EXPECT_EQ(stateChan.get()->value(), val + 3);
    }

    observerTest.observer().setState(0);

    {
        nhope::Chan<ObservableState<int>> stateChan;
        stateChan.attachToProduser(observerTest.observer());

        const int val = stateChan.get()->value();
        EXPECT_LT(val, StateObserverTest::startValue);
        EXPECT_EQ(stateChan.get()->value(), val + 1);
        EXPECT_EQ(stateChan.get()->value(), val + 2);
        EXPECT_EQ(stateChan.get()->value(), val + 3);
    }
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
    // NOLINTNEXTLINE
    EXPECT_THROW(nhope::StateObserver<int> observer(nullptr, nullptr, nhope::ThreadPoolExecutor::defaultExecutor()),
                 nhope::StateUninitialized);
}

TEST(StateObserver, Exception)   // NOLINT
{
    using namespace nhope;
    constexpr auto initValue = 0;
    constexpr auto magic = 42;

    std::atomic<int> value = initValue;

    StateObserver<int> observer(
      [&](int newVal) {
          if (newVal == magic) {
              throw std::runtime_error("magic set");
          }
          value = newVal;
          return makeReadyFuture();
      },
      [&] {
          const auto current = value.load();
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
      nhope::ThreadPoolExecutor::defaultExecutor(), 10ms);

    Chan<ObservableState<int>> stateChan;

    /* Устанавливаем magic и только затем подключаемся к observer-у.
       Если сделать в другом порядке, иногда в канал будет успевать попадать initValue
       из-за чего тест не будет проходить.
       https://gitlab.olimp.lan/alekseev/nhope/-/issues/9  */
    observer.setState(magic);
    stateChan.attachToProduser(observer);

    EXPECT_EQ(stateChan.get().value(), magic);
    EXPECT_TRUE(stateChan.get()->hasException());   // setter: throw Exception
    EXPECT_EQ(stateChan.get().value(), 0);          // getter: return current value (0)

    observer.setState(magic + 1);
    EXPECT_EQ(stateChan.get().value(), magic + 1);
    EXPECT_TRUE(stateChan.get()->hasException());   // getter: update value (2) and throw Exception
    EXPECT_EQ(stateChan.get().value(), 2);          // getter: return current value (2)

    observer.setState(magic + 2);
    EXPECT_EQ(stateChan.get().value(), magic + 2);
    EXPECT_TRUE(stateChan.get()->hasException());   // getter throw Exception
}

TEST(StateObserver, AsyncExceptionInSetter)   // NOLINT
{
    using namespace nhope;
    constexpr auto magic = 42;

    AOContext aoCtx(nhope::ThreadPoolExecutor::defaultExecutor());

    StateObserver<int> observer(
      [&](int /*unused*/) {
          return makeReadyFuture().then(aoCtx, [] {
              throw std::runtime_error("magic set");
          });
      },
      [&] {
          return makeReadyFuture<int>(magic);
      },
      nhope::ThreadPoolExecutor::defaultExecutor(), 1000s);

    Chan<ObservableState<int>> stateChan;

    stateChan.attachToProduser(observer);
    EXPECT_EQ(stateChan.get(), magic);
    observer.setState(magic + 1);

    EXPECT_EQ(stateChan.get(), magic + 1);
    EXPECT_TRUE(stateChan.get()->hasException());   // NOLINT
}
