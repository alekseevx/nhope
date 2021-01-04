#pragma once

#include <exception>

#include "../../async/ao-context.h"
#include "../../async/future.h"
#include "../consumer.h"

namespace nhope::detail {

template<typename T>
class TakeOneConsumer final : public Consumer<T>
{
public:
    ~TakeOneConsumer() override
    {
        if (!m_full) {
            auto exPtr = std::make_exception_ptr(AsyncOperationWasCancelled());
            m_promise.setException(exPtr);
        }
    }

    Future<T> future()
    {
        return m_promise.future();
    }

public:   // Consumer<T>
    typename Consumer<T>::Status consume(const T& value) override
    {
        if (m_full) {
            return Consumer<T>::Status::Closed;
        }

        m_full = true;
        m_promise.setValue(value);
        return Consumer<T>::Status::Closed;
    }

private:
    bool m_full = false;
    Promise<T> m_promise;
};

}   // namespace nhope::detail
