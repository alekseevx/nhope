#pragma once

#include <list>
#include <memory>
#include <mutex>

#include "nhope/async/reverse-lock.h"
#include "nhope/seq/consumer.h"

namespace nhope {

template<typename T>
class ConsumerList final : public Consumer<T>
{
public:
    void close()
    {
        std::scoped_lock lock(m_mutex);
        m_closed = true;
        m_list.clear();
    }

    void addConsumer(std::unique_ptr<Consumer<T>> consumer)
    {
        std::scoped_lock lock(m_mutex);
        if (m_closed) {
            return;
        }

        m_list.emplace_back(std::move(consumer));
    }

    // Consumer
    typename Consumer<T>::Status consume(const T& value) override
    {
        std::unique_lock lock(m_mutex);
        if (m_closed) {
            return Consumer<T>::Status::Closed;
        }
        List list;
        list.splice(list.begin(), m_list);

        {
            nhope::ReverseLock unlock(lock);
            auto it = list.begin();
            while (it != list.end()) {
                switch (exceptionSafeConsume(*it, value)) {
                case Consumer<T>::Status::Ok:
                    it++;
                    break;
                case Consumer<T>::Status::Closed:
                    it = list.erase(it);
                    break;
                }
            }
        }

        m_list.splice(m_list.begin(), list);
        return Consumer<T>::Status::Ok;
    }

private:
    static typename Consumer<T>::Status exceptionSafeConsume(std::unique_ptr<Consumer<T>>& consumer,
                                                             const T& value) noexcept
    {
        try {
            return consumer->consume(value);
        } catch (...) {
            // The consumer did not return Closed - we will not delete it
            return Consumer<T>::Status::Ok;
        }
    }

    using List = std::list<std::unique_ptr<Consumer<T>>>;

    std::mutex m_mutex;
    bool m_closed = false;
    List m_list;
};

}   // namespace nhope
