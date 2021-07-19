#pragma once

#include <algorithm>
#include <list>
#include <memory>
#include <utility>
#include <limits>

#include "nhope/async/ao-context.h"

namespace nhope::detail {

struct AOHandlerRec final
{
    AOHandlerRec() noexcept = default;

    AOHandlerRec(AOHandlerRec&&) noexcept = default;
    AOHandlerRec& operator=(AOHandlerRec&&) noexcept = default;

    ~AOHandlerRec() noexcept = default;

    AOHandlerId id = invalidId;
    std::unique_ptr<AOHandler> handler;
};

class AOHandlerStorage final
{
public:
    AOHandlerStorage() = default;
    AOHandlerStorage(AOHandlerStorage&&) noexcept = default;

    ~AOHandlerStorage()
    {
        this->cancelAll();
    }

    void put(AOHandlerId id, std::unique_ptr<AOHandler> handler)
    {
        m_storage.emplace_back(AOHandlerRec{id, std::move((handler))});
    }

    std::unique_ptr<AOHandler> get(AOHandlerId id)
    {
        const auto iter = std::find_if(m_storage.begin(), m_storage.end(), [id](auto& r) {
            return r.id == id;
        });
        if (iter == m_storage.end()) {
            return nullptr;
        }

        auto handler = std::move(iter->handler);
        m_storage.erase(iter);

        return std::move(handler);
    }

    void cancelAll()
    {
        for (auto& r : m_storage) {
            try {
                r.handler->cancel();
            } catch (...) {
            }
        }

        m_storage.clear();
    }

private:
    std::list<AOHandlerRec> m_storage;
};

}   // namespace nhope::detail