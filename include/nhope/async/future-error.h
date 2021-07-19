#pragma once

#include <stdexcept>
#include <string_view>

namespace nhope {

class FutureError : public std::logic_error
{
public:
    explicit FutureError(std::string_view errMessage);
};

class PromiseAlreadySatisfiedError final : public FutureError
{
public:
    PromiseAlreadySatisfiedError();
};

class BrokenPromiseError final : public FutureError
{
public:
    BrokenPromiseError();
};

class FutureNoStateError final : public FutureError
{
public:
    FutureNoStateError();
};

class FutureAlreadyRetrievedError final : public FutureError
{
public:
    FutureAlreadyRetrievedError();
};

class MakeFutureChainAfterWaitError final : public FutureError
{
public:
    MakeFutureChainAfterWaitError();
};

}   // namespace nhope
