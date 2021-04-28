#pragma once

#include <deque>
#include <list>
#include <mutex>
#include <memory>

#include "consumer.h"
#include "nhope/async/reverse_lock.h"

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
        List list = std::move(m_list);

        {
            nhope::ReverseLock unlock(lock);
            auto it = list.begin();
            while (it != list.end()) {
                switch ((*it)->consume(value)) {
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
    using List = std::list<std::unique_ptr<Consumer<T>>>;

    std::mutex m_mutex;
    bool m_closed = false;
    List m_list;
};

}   // namespace nhope
