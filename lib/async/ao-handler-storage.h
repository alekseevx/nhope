#pragma once

#include <algorithm>
#include <vector>
#include <memory>
#include <utility>

#include "nhope/async/ao-handler.h"
#include "nhope/async/detail/ao-handler-id.h"

namespace nhope::detail {

struct AOHandlerRec final
{
    AOHandlerRec() noexcept = default;

    AOHandlerRec(AOHandlerRec&&) noexcept = default;
    AOHandlerRec& operator=(AOHandlerRec&&) noexcept = default;

    ~AOHandlerRec() noexcept = default;

    AOHandlerId id = invalidAOHandlerId;
    std::unique_ptr<AOHandler> handler;
};

class AOHandlerStorage final
{
public:
    AOHandlerStorage() = default;

    AOHandlerStorage(AOHandlerStorage&&) noexcept = default;
    AOHandlerStorage& operator=(AOHandlerStorage&&) noexcept = default;

    ~AOHandlerStorage()
    {
        this->cancelAll();
    }

    void put(AOHandlerId id, std::unique_ptr<AOHandler> handler)
    {
        auto& rec = this->findFreeRec();
        rec.id = id;
        rec.handler = std::move(handler);
    }

    std::unique_ptr<AOHandler> get(AOHandlerId id) noexcept
    {
        const auto it = std::find_if(m_storage.begin(), m_storage.end(), [id](const auto& r) {
            return r.id == id;
        });
        if (it == m_storage.end()) {
            return nullptr;
        }

        return freeRec(*it);
    }

    void cancelAll() noexcept
    {
        for (auto& r : m_storage) {
            try {
                if (r.handler != nullptr) {
                    r.handler->cancel();
                }
            } catch (...) {
            }
        }

        m_storage.clear();
    }

private:
    AOHandlerRec& findFreeRec()
    {
        const auto it = std::find_if(m_storage.begin(), m_storage.end(), [](const auto& r) {
            return r.id == invalidAOHandlerId;
        });
        if (it == m_storage.end()) {
            return m_storage.emplace_back();
        }

        return *it;
    }

    static std::unique_ptr<AOHandler> freeRec(AOHandlerRec& rec) noexcept
    {
        rec.id = invalidAOHandlerId;
        return std::move(rec.handler);
    }

    std::vector<AOHandlerRec> m_storage;
};

}   // namespace nhope::detail
