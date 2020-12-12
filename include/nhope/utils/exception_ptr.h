#pragma once

#include <exception>
#include <boost/exception_ptr.hpp>

namespace nhope::utils {

std::exception_ptr toStdExceptionPtr(const boost::exception_ptr& ex);
boost::exception_ptr toBoostExceptionPtr(const std::exception_ptr& ex);

}   // namespace nhope::utils
