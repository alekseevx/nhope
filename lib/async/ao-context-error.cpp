#include <string>
#include "nhope/async/ao-context-error.h"

namespace nhope {

AsyncOperationWasCancelled::AsyncOperationWasCancelled()
  : std::runtime_error("AsyncOperationWasCancelled")
{}

AsyncOperationWasCancelled::AsyncOperationWasCancelled(std::string_view errMessage)
  : std::runtime_error(std::string(errMessage))
{}

AOContextClosed::AOContextClosed()
  : std::runtime_error("AOContextClosed")
{}

DetectedDeadlock::DetectedDeadlock()
  : std::runtime_error("DetectedDeadlock")
{}

}   // namespace nhope
