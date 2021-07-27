#pragma once

#include <atomic>
#include <cassert>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace nhope::detail {

template<typename T>
class RefCounter final
{
public:
    template<typename... Args>
    RefCounter(Args&&... args)
      : m_data(std::forward<Args>(args)...)
    {}

    void addRef() noexcept
    {
        m_ref.fetch_add(1, std::memory_order_relaxed);
    }

    void release() noexcept
    {
        if (m_ref.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            delete this;
        }
    }

    [[nodiscard]] int refCount() const noexcept
    {
        return m_ref.load(std::memory_order_relaxed);
    }

    T* data()
    {
        return &m_data;
    }

private:
    ~RefCounter() = default;

    mutable std::atomic<int> m_ref = 1;
    T m_data;
};

template<typename T>
class RefPtr final
{
    template<typename Tp, typename... Args>
    friend RefPtr<Tp> makeRefPtr(Args&&... args);

public:
    RefPtr(std::nullptr_t = nullptr) noexcept
    {}

    RefPtr(const RefPtr& other) noexcept
      : m_ptr(other.m_ptr)
    {
        if (m_ptr != nullptr) {
            m_ptr->addRef();
        }
    }

    RefPtr(RefPtr&& other) noexcept
      : m_ptr(other.m_ptr)
    {
        other.m_ptr = nullptr;
    }

    RefPtr& operator=(const RefPtr& other)
    {
        if (this == &other) {
            return *this;
        }

        if (other.m_ptr != nullptr) {
            other.m_ptr->addRef();
        }

        this->release();
        m_ptr = other.m_ptr;

        return *this;
    }

    RefPtr& operator=(RefPtr&& other) noexcept
    {
        this->release();

        m_ptr = other.m_ptr;
        other.m_ptr = nullptr;
        return *this;
    }

    ~RefPtr() noexcept
    {
        this->release();
    }

    T& operator*() const noexcept
    {
        assert(m_ptr != nullptr);
        return *m_ptr->data();
    }

    T* operator->() const noexcept
    {
        assert(m_ptr != nullptr);
        return m_ptr->data();
    }

    T* get() const noexcept
    {
        if (m_ptr == nullptr) {
            return nullptr;
        }
        return m_ptr->data();
    }

    bool operator==(std::nullptr_t) const noexcept
    {
        return m_ptr == nullptr;
    }

    bool operator!=(std::nullptr_t) const noexcept
    {
        return m_ptr != nullptr;
    }

    [[nodiscard]] int refCount() const noexcept
    {
        if (m_ptr == nullptr) {
            return 0;
        }
        return m_ptr->refCount();
    }

    void release()
    {
        if (m_ptr != nullptr) {
            m_ptr->release();
            m_ptr = nullptr;
        }
    }

private:
    explicit RefPtr(RefCounter<T>* ptr) noexcept
      : m_ptr(ptr)
    {}

    RefCounter<T>* m_ptr = nullptr;
};

template<typename T, typename... Args>
RefPtr<T> makeRefPtr(Args&&... args)
{
    return RefPtr<T>(new RefCounter<T>(std::forward<Args>(args)...));
}

};   // namespace nhope::detail
