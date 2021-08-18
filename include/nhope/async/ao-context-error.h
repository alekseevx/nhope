#pragma once

#include <stdexcept>
#include <string_view>

namespace nhope {

class AsyncOperationWasCancelled final : public std::runtime_error
{
public:
    AsyncOperationWasCancelled();
    explicit AsyncOperationWasCancelled(std::string_view errMessage);
};

class AOContextClosed final : public std::runtime_error
{
public:
    AOContextClosed();
};

class DetectedDeadlock final : public std::runtime_error
{
public:
    DetectedDeadlock();
};

}   // namespace nhope
