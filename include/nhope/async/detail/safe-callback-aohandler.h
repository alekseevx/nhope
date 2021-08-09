#pragma once

#include <memory>
#include "nhope/async/ao-context.h"

namespace nhope::detail {

template<typename Callback>
class SafeCallbackAOHandler final : public AOHandler
{
public:
    explicit SafeCallbackAOHandler(Callback callback)
      : m_callback(std::move(callback))
    {}

    void call() override
    {
        m_callback();
    }

    void cancel() override
    {}

private:
    Callback m_callback;
};

template<typename Callback>
std::unique_ptr<AOHandler> makeSafeCallbackAOHandler(Callback callback)
{
    return std::make_unique<SafeCallbackAOHandler<Callback>>(std::move(callback));
}

}   // namespace nhope::detail
