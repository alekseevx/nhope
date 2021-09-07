#pragma once

#include <atomic>
#include <cassert>
#include <cstdint>
#include <thread>

namespace nhope::detail {

class AOContextState final
{
public:
    enum Flags : std::uint64_t
    {
        PreparingForClosing = 1 << 0,
        Closing = 1 << 1,
        Closed = 1 << 2,
    };
    static constexpr auto flagsMask = std::uint64_t(0xFF);
    static constexpr auto refCounterOffset = 8;
    static constexpr auto refCounterMask = std::uint64_t(0xFF'FF'FF'FF) << refCounterOffset;
    static constexpr auto blockCloseCounterOffset = 40;
    static constexpr auto blockCloseCounterMask = std::uint64_t(0xFF'FF'FF) << blockCloseCounterOffset;

    static constexpr auto oneRef = std::uint64_t(1) << refCounterOffset;
    static constexpr auto oneBlockClose = std::uint64_t(1) << blockCloseCounterOffset;

    [[nodiscard]] bool blockClose() noexcept
    {
        const auto oldState = m_state.fetch_add(oneBlockClose, std::memory_order_relaxed);
        if ((oldState & Flags::PreparingForClosing) != 0) {
            this->unblockClose();
            return false;
        };

        return true;
    }

    [[nodiscard]] bool blockCloseAndAddRef() noexcept
    {
        const auto oldState = m_state.fetch_add(oneBlockClose | oneRef, std::memory_order_relaxed);
        if ((oldState & Flags::PreparingForClosing) != 0) {
            this->unblockCloseAndRemoveRef();
            return false;
        };

        return true;
    }

    void addRef() noexcept
    {
        m_state.fetch_add(oneRef, std::memory_order_relaxed);
    }

    bool removeRef() noexcept
    {
        const auto oldState = m_state.fetch_sub(oneRef, std::memory_order_acq_rel);
        return (oldState & refCounterMask) == oneRef;
    }

    void unblockClose() noexcept
    {
        m_state.fetch_sub(oneBlockClose, std::memory_order_acq_rel);
    }

    bool unblockCloseAndRemoveRef() noexcept
    {
        const auto oldState = m_state.fetch_sub(oneBlockClose | oneRef, std::memory_order_acq_rel);
        return (oldState & refCounterMask) == oneRef;
    }

    // Returns true if we are the first to call startClose
    bool startClose() noexcept
    {
        const auto oldState = m_state.fetch_or(Flags::PreparingForClosing, std::memory_order_relaxed);
        return (oldState & Flags::PreparingForClosing) == 0;
    }

    [[nodiscard]] bool isOpen() const noexcept
    {
        return (m_state.load(std::memory_order_relaxed) & Flags::PreparingForClosing) == 0;
    }

    void waitForClosed() const noexcept
    {
        while ((m_state.load(std::memory_order_acquire) & Flags::Closed) == 0) {
            std::this_thread::yield();   // FIXME: Use atomic::wait from C++20
        }
    }

    [[nodiscard]] bool isClosed() const noexcept
    {
        return (m_state.load(std::memory_order_relaxed) & Flags::Closed) != 0;
    }

    void setClosingFlag() noexcept
    {
        [[maybe_unused]] const auto oldState = m_state.fetch_or(Flags::Closing, std::memory_order_acq_rel);
        assert((oldState & Flags::Closing) == 0);   // NOLINT
    }

    void setClosedFlag() noexcept
    {
        [[maybe_unused]] const auto oldState = m_state.fetch_or(Flags::Closed, std::memory_order_acq_rel);
        assert((oldState & Flags::Closed) == 0);   // NOLINT
    }

    [[nodiscard]] std::size_t getBlockCloseCounter() const noexcept
    {
        const auto state = m_state.load(std::memory_order_relaxed);
        const auto blockCloseCounter = (state & blockCloseCounterMask) >> blockCloseCounterOffset;
        return static_cast<std::size_t>(blockCloseCounter);
    }

private:
    std::atomic<std::uint64_t> m_state = oneRef;
};

}   // namespace nhope::detail
