#include <exception>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

#include <nhope/utils/exception_ptr.h>
#include <nhope/async/thread-executor.h>
#include "nhope/async/async-invoke.h"

using namespace nhope::utils;
using namespace std::literals;

namespace {

class TestException : public std::runtime_error
{
public:
    explicit TestException(const std::string& msg)
      : std::runtime_error(msg.data())
    {}
};

}   // namespace

TEST(ExceptionPtr, toStdExceptionPtr)   // NOLINT
{
    const auto testMsg = "TestTest"s;

    auto boostPtr = boost::copy_exception(TestException(testMsg));
    auto stdPtr = toStdExceptionPtr(boostPtr);

    try {
        std::rethrow_exception(stdPtr);
    } catch (const TestException& ex) {
        EXPECT_EQ(ex.what(), testMsg);
    } catch (...) {
        FAIL() << "Exception type not saved";
    }
}

TEST(ExceptionPtr, toBoostExceptionPtr)   // NOLINT
{
    const auto testMsg = "TestTestTest"s;

    auto stdPtr = std::make_exception_ptr(TestException(testMsg));
    auto boostPtr = toBoostExceptionPtr(stdPtr);

    try {
        boost::rethrow_exception(boostPtr);
    } catch (const TestException& ex) {
        EXPECT_EQ(ex.what(), testMsg);
    } catch (...) {
        FAIL() << "Exception type not saved";
    }
}

TEST(ExceptionPtr, customException)   // NOLINT
{
    nhope::ThreadExecutor executor;
    nhope::AOContext ctx(executor);

    EXPECT_THROW(nhope::invoke(ctx,   //NOLINT
                               [] {
                                   throw TestException("test");
                               }),
                 TestException);
}