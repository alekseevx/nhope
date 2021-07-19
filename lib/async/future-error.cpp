#include <stdexcept>
#include <string>
#include <string_view>

#include "nhope/async/future-error.h"

namespace nhope {

using namespace std::literals;

FutureError::FutureError(std::string_view errMessage)
  : std::logic_error(std::string(errMessage))
{}

PromiseAlreadySatisfiedError::PromiseAlreadySatisfiedError()
  : FutureError("PromiseAlreadySatisfied"sv)
{}

BrokenPromiseError::BrokenPromiseError()
  : FutureError("BrokenPromise"sv)
{}

FutureNoStateError::FutureNoStateError()
  : FutureError("FutureNoState"sv)
{}

FutureAlreadyRetrievedError::FutureAlreadyRetrievedError()
  : FutureError("FutureAlreadyRetrieved"sv)
{}

MakeFutureChainAfterWaitError::MakeFutureChainAfterWaitError()
  : FutureError("MakeFutureChainAfterWaitError")
{}

}   // namespace nhope
