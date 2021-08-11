#include <memory>
#include <stdexcept>
#include <tuple>
#include <vector>

#include <gtest/gtest.h>

#include "nhope/seq/consumer-list.h"
#include "nhope/seq/consumer.h"

namespace {
using namespace nhope;

class TestConsumer final : public Consumer<int>
{
public:
    static constexpr auto invalidValue = -1;

    TestConsumer(std::shared_ptr<int> consumeCallCounter, std::shared_ptr<int> expectValue)
      : m_consumeCallCounter(std::move(consumeCallCounter))
      , m_expectValue(std::move(expectValue))
    {}

    std::shared_ptr<bool> closeFlag()
    {
        return m_closeFlag;
    }

    Status consume(const int& value) override
    {
        if (m_consumeCallCounter) {
            *m_consumeCallCounter += 1;
        }

        if (m_expectValue) {
            EXPECT_EQ(*m_expectValue, value);
        }

        if (value == invalidValue) {
            throw std::runtime_error("Invalid value");
        }

        return *m_closeFlag ? Status::Closed : Status::Ok;
    }

private:
    std::shared_ptr<bool> m_closeFlag = std::make_shared<bool>(false);
    std::shared_ptr<int> m_consumeCallCounter;
    std::shared_ptr<int> m_expectValue;
};

}   // namespace

TEST(ConsumerList, addAndConsume)   // NOLINT
{
    constexpr auto startValue = 1000;

    auto consumeCallCounter = std::make_shared<int>(0);
    auto expectValue = std::make_shared<int>(startValue);

    ConsumerList<int> consumerList;
    consumerList.consume(*expectValue);
    EXPECT_EQ(*consumeCallCounter, 0);

    *expectValue += 1;
    consumerList.addConsumer(std::make_unique<TestConsumer>(consumeCallCounter, expectValue));
    consumerList.consume(*expectValue);
    EXPECT_EQ(*consumeCallCounter, 1);

    *expectValue += 1;
    *consumeCallCounter = 0;
    consumerList.addConsumer(std::make_unique<TestConsumer>(consumeCallCounter, expectValue));
    consumerList.consume(*expectValue);
    EXPECT_EQ(*consumeCallCounter, 2);
}

TEST(ConsumerList, autoRemove)   // NOLINT
{
    constexpr auto startValue = 1000;

    auto consumeCallCounter = std::make_shared<int>(0);
    auto expectValue = std::make_shared<int>(startValue);

    ConsumerList<int> consumerList;
    auto consumer = std::make_unique<TestConsumer>(consumeCallCounter, expectValue);
    auto closeFlag = consumer->closeFlag();
    consumerList.addConsumer(std::move(consumer));

    consumerList.consume(*expectValue);
    EXPECT_EQ(*consumeCallCounter, 1);

    /* Consumer получит новой значение и затем будет удален т.к. он вернет статус Status::Closed */
    *consumeCallCounter = 0;
    *closeFlag = true;
    consumerList.consume(*expectValue);
    EXPECT_EQ(*consumeCallCounter, 1);

    /* Проверим, что Consumer был удален из списка */
    *consumeCallCounter = 0;
    consumerList.consume(*expectValue);
    EXPECT_EQ(*consumeCallCounter, 0);
}

TEST(ConsumerList, close)   // NOLINT
{
    constexpr auto startValue = 1000;

    auto consumeCallCounter = std::make_shared<int>(0);
    auto expectValue = std::make_shared<int>(startValue);

    ConsumerList<int> consumerList;

    consumerList.addConsumer(std::make_unique<TestConsumer>(consumeCallCounter, expectValue));
    consumerList.consume(*expectValue);
    EXPECT_EQ(*consumeCallCounter, 1);

    /* Проверим, что Consumer был удален из списка */
    *consumeCallCounter = 0;
    consumerList.close();
    consumerList.consume(*expectValue);
    EXPECT_EQ(*consumeCallCounter, 0);

    /* Проверим, что Consumer-ы больше на добавляются */
    *consumeCallCounter = 0;
    consumerList.addConsumer(std::make_unique<TestConsumer>(consumeCallCounter, expectValue));
    consumerList.consume(*expectValue);
    EXPECT_EQ(*consumeCallCounter, 0);
}

TEST(ConsumerList, exceptionInConsumer)   // NOLINT
{
    auto consumeCallCounter = std::make_shared<int>(0);
    auto expectValue = std::make_shared<int>();

    ConsumerList<int> consumerList;

    consumerList.addConsumer(std::make_unique<TestConsumer>(consumeCallCounter, expectValue));

    /* Consumer генерирует исключение. */
    *consumeCallCounter = 0;
    *expectValue = TestConsumer::invalidValue;
    consumerList.consume(*expectValue);
    EXPECT_EQ(*consumeCallCounter, 1);

    /* Проверим, что consumerList не потерял Consumer из-за исключения */
    *consumeCallCounter = 0;
    *expectValue = 0;
    consumerList.consume(*expectValue);
    EXPECT_EQ(*consumeCallCounter, 1);
}
