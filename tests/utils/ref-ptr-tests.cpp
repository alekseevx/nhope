#include <atomic>
#include <list>
#include <memory>
#include <string>
#include <utility>
#include <thread>

#include <gtest/gtest.h>
#include "nhope/utils/detail/ref-ptr.h"

namespace {

using namespace nhope::detail;
using namespace std::literals;

constexpr int etaloneData = 1000;
constexpr int etaloneData2 = 1001;

class TestClass final
{
public:
    explicit TestClass(int d)
      : m_data(d)
    {
        ++instaceCount;
    }

    ~TestClass()
    {
        --instaceCount;
    }

    [[nodiscard]] int data() const
    {
        return m_data;
    }

    static std::atomic<int> instaceCount;   // NOLINT

private:
    int m_data;
};

std::atomic<int> TestClass::instaceCount = 0;   // NOLINT

}   // namespace

TEST(RefPtr, NullPtr)   // NOLINT
{
    RefPtr<TestClass> ptr;
    EXPECT_TRUE(ptr == nullptr);
    EXPECT_FALSE(ptr != nullptr);
    EXPECT_EQ(ptr.get(), nullptr);
    EXPECT_EQ(ptr.refCount(), 0);
}

TEST(RefPtr, Make)   // NOLINT
{
    {
        auto ptr = makeRefPtr<TestClass>(etaloneData);
        EXPECT_EQ(TestClass::instaceCount, 1);

        EXPECT_EQ(ptr.refCount(), 1);
        EXPECT_FALSE(ptr == nullptr);
        EXPECT_TRUE(ptr != nullptr);
        EXPECT_EQ(ptr->data(), etaloneData);
        EXPECT_EQ((*ptr).data(), etaloneData);
    }

    EXPECT_EQ(TestClass::instaceCount, 0);
}

TEST(RefPtr, CopyConstructor)   // NOLINT
{
    {
        auto ptr = makeRefPtr<TestClass>(etaloneData);
        TestClass* rawPtr = ptr.get();

        auto ptr2 = ptr;   // NOLINT
        EXPECT_EQ(ptr2.get(), rawPtr);
        EXPECT_EQ(ptr2.refCount(), 2);
        EXPECT_EQ(ptr2->data(), etaloneData);
    }

    EXPECT_EQ(TestClass::instaceCount, 0);
}

TEST(RefPtr, CopyOperator)   // NOLINT
{
    {
        auto ptr = makeRefPtr<TestClass>(etaloneData);

        RefPtr<TestClass> ptr2;
        ptr2 = ptr;
        EXPECT_EQ(ptr2.get(), ptr.get());
        EXPECT_EQ(ptr2.refCount(), 2);
        EXPECT_EQ(ptr2->data(), etaloneData);

        ptr2 = ptr2;
        EXPECT_EQ(ptr2.get(), ptr.get());
        EXPECT_EQ(ptr2.refCount(), 2);
        EXPECT_EQ(ptr2->data(), etaloneData);

        auto ptr3 = makeRefPtr<TestClass>(etaloneData2);
        ptr2 = ptr3;
        EXPECT_EQ(ptr.refCount(), 1);
        EXPECT_EQ(ptr->data(), etaloneData);
        EXPECT_EQ(ptr2.get(), ptr3.get());
        EXPECT_EQ(ptr2.refCount(), 2);
        EXPECT_EQ(ptr2->data(), etaloneData2);

        RefPtr<TestClass> ptr4;
        ptr2 = ptr4;
        EXPECT_EQ(ptr2, nullptr);
        EXPECT_EQ(ptr2.refCount(), 0);
    }

    EXPECT_EQ(TestClass::instaceCount, 0);
}

TEST(RefPtr, MoveConstructor)   // NOLINT
{
    {
        auto ptr = makeRefPtr<TestClass>(etaloneData);
        TestClass* rawPtr = ptr.get();

        auto ptr2 = std::move(ptr);
        EXPECT_EQ(ptr.get(), nullptr);
        EXPECT_EQ(ptr2.get(), rawPtr);
        EXPECT_EQ(ptr2.refCount(), 1);
        EXPECT_EQ(ptr2->data(), etaloneData);

        auto ptr3 = std::move(RefPtr<TestClass>());
        EXPECT_EQ(ptr3.get(), nullptr);
        EXPECT_EQ(ptr3.refCount(), 0);
    }

    EXPECT_EQ(TestClass::instaceCount, 0);
}

TEST(RefPtr, MoveOperator)   // NOLINT
{
    auto ptr = makeRefPtr<TestClass>(etaloneData);
    TestClass* rawPtr = ptr.get();

    auto ptr2 = std::move(ptr);
    EXPECT_EQ(ptr.get(), nullptr);
    EXPECT_EQ(ptr2.get(), rawPtr);
    EXPECT_EQ(ptr2.refCount(), 1);
    EXPECT_EQ(ptr2->data(), etaloneData);

    RefPtr<TestClass> ptr3;
    ptr3 = std::move(ptr2);
    EXPECT_EQ(ptr.get(), nullptr);
    EXPECT_EQ(ptr3.get(), rawPtr);
    EXPECT_EQ(ptr3.refCount(), 1);
    EXPECT_EQ(ptr3->data(), etaloneData);

    ptr3 = std::move(RefPtr<TestClass>());
    EXPECT_EQ(ptr3.get(), nullptr);
    EXPECT_EQ(ptr3.refCount(), 0);

    EXPECT_EQ(TestClass::instaceCount, 0);
}

TEST(RefPtr, ThreadConcurrency)   // NOLINT
{
    static constexpr int iterCount = 10000;

    {
        auto ptr = makeRefPtr<TestClass>(etaloneData);

        auto test = [&] {
            for (int i = 0; i < iterCount; ++i) {
                auto ptr2 = ptr;
                RefPtr<TestClass> ptr3;
                ptr3 = ptr;
                auto ptr4 = std::move(ptr2);

                EXPECT_GE(ptr.refCount(), 3);
                std::this_thread::yield();
            }
        };

        auto thOne = std::thread(test);
        auto thTwo = std::thread(test);

        thOne.join();
        thTwo.join();

        EXPECT_EQ(ptr.refCount(), 1);
    }

    EXPECT_EQ(TestClass::instaceCount, 0);
}