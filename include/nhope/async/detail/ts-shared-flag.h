#pragma once

#include <atomic>
#include <cassert>
#include <cstddef>
#include <memory>

namespace nhope::detail {

class SharedFlag final
{
public:
    SharedFlag() = default;

    SharedFlag(const SharedFlag& other) noexcept
    {
        other.copyRef(m_ptr);
    }

    SharedFlag& operator=(const SharedFlag& other) noexcept
    {
        if (this != &other) {
            this->removeRef();
            other.copyRef(m_ptr);
        }
        return *this;
    }

    ~SharedFlag() noexcept
    {
        this->removeRef();
    }

    [[nodiscard]] bool isSet() const noexcept
    {
        const auto* ptr = m_ptr.load(std::memory_order_acquire);
        return ptr != nullptr && (ptr->load(std::memory_order_relaxed) & flagMask) != 0;
    }

    void set()
    {
        auto* ptr = this->lazyCreate();
        ptr->fetch_or(1, std::memory_order_relaxed);
    }

private:
    static constexpr std::size_t flagMask = 1;
    static constexpr std::size_t oneRef = 1 << 1;

    void copyRef(std::atomic<std::atomic<std::size_t>*>& ptr) const noexcept
    {
        auto* flag = this->lazyCreate();
        flag->fetch_add(oneRef, std::memory_order_relaxed);
        ptr.store(flag, std::memory_order_release);
    }

    void removeRef() noexcept
    {
        if (auto* ptr = m_ptr.load(std::memory_order_acquire)) {
            if ((ptr->fetch_sub(oneRef, std::memory_order_acq_rel) & ~flagMask) == oneRef) {
                // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
                delete ptr;

                m_ptr.store(nullptr, std::memory_order_release);
            }
        }
    }

    std::atomic<std::size_t>* lazyCreate() const
    {
        if (auto* ptr = m_ptr.load(std::memory_order_acquire)) {
            return ptr;
        }

        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        auto* newFlag = new std::atomic<std::size_t>(oneRef);

        std::atomic<std::size_t>* expected = nullptr;
        if (m_ptr.compare_exchange_strong(expected, newFlag)) {
            return newFlag;
        }

        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        delete newFlag;

        return expected;
    }

    mutable std::atomic<std::atomic<std::size_t>*> m_ptr = nullptr;
};

}   // namespace nhope::detail
