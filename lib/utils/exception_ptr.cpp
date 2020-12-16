#include <exception>
#include <memory>
#include <utility>

#include <boost/exception/detail/exception_ptr.hpp>
#include <boost/exception/exception.hpp>
#include <boost/smart_ptr.hpp>

#include "nhope/utils/exception_ptr.h"

namespace {

class StdExceptionPtrWrapper final : public boost::exception_detail::clone_base
{
public:
    explicit StdExceptionPtrWrapper(std::exception_ptr ptr)
      : m_ptr(std::move(ptr))
    {}

    [[nodiscard]] const clone_base* clone() const override
    {
        return new StdExceptionPtrWrapper(m_ptr);
    }

    void rethrow() const override
    {
        std::rethrow_exception(m_ptr);
    }

private:
    std::exception_ptr m_ptr;
};

}   // namespace

std::exception_ptr nhope::utils::toStdExceptionPtr(const boost::exception_ptr& ex)
{
    try {
        boost::rethrow_exception(ex);
    } catch (...) {
        return std::current_exception();
    }
}

boost::exception_ptr nhope::utils::toBoostExceptionPtr(const std::exception_ptr& ex)
{
    auto w = boost::make_shared<StdExceptionPtrWrapper>(ex);
    return boost::exception_ptr(w);
}
