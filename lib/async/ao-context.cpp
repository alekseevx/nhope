
#include <nhope/async/ao-context.h>
#include <nhope/async/thread-executor.h>

namespace {

using namespace nhope;

}   // namespace

AsyncOperationWasCancelled::AsyncOperationWasCancelled()
  : std::runtime_error("AsyncOperationWasCancelled")
{}

AsyncOperationWasCancelled::AsyncOperationWasCancelled(std::string_view errMessage)
  : std::runtime_error(errMessage.data())
{}
