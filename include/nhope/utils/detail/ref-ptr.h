#pragma once

#include <cassert>
#include <atomic>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace nhope::detail {

class BaseRefCounter
{
public:
    void addRef() noexcept
    {
        m_ref.fetch_add(1, std::memory_order_relaxed);
    }

    [[nodiscard]] bool release() noexcept
    {
        return m_ref.fetch_sub(1, std::memory_order_acq_rel) == 1;
    }

    [[nodiscard]] std::size_t refCount() const noexcept
    {
        return m_ref.load(std::memory_order_relaxed);
    }

private:
    std::atomic<std::size_t> m_ref = 1;
};

template<typename T>
class RefCounterImpl final : public BaseRefCounter

{
public:
    template<typename... Args>
    explicit RefCounterImpl(Args&&... args)
      : m_date(std::forward<Args>(args)...)
    {}

    T* data() noexcept
    {
        return &m_date;
    }

private:
    T m_date;
};

template<typename T>
struct RefCounterT
{
    using Type = std::conditional_t<std::is_base_of_v<BaseRefCounter, T>, T, RefCounterImpl<T>>;
};

template<typename T>
using RefCounter = typename RefCounterT<T>::Type;

struct NotAddRefTag;

template<typename T>
class RefPtr final
{
    template<typename Tp, typename... Args>
    friend RefPtr<Tp> makeRefPtr(Args&&... args);

    template<typename Tp>
    friend RefPtr<Tp> refPtrFromRawPtr(Tp* rawPtr);

    template<typename Tp>
    friend RefPtr<Tp> refPtrFromRawPtr(Tp* rawPtr, NotAddRefTag /*unused*/);

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

    RefPtr& operator=(const RefPtr& other) noexcept
    {
        if (this != &other) {
            this->release();
            m_ptr = other.m_ptr;
            if (m_ptr != nullptr) {
                m_ptr->addRef();
            }
        }
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
        return *this->data();
    }

    T* operator->() const noexcept
    {
        assert(m_ptr != nullptr);
        return this->data();
    }

    T* get() const noexcept
    {
        return m_ptr ? this->data() : nullptr;
    }

    bool operator==(std::nullptr_t) const noexcept
    {
        return m_ptr == nullptr;
    }

    bool operator!=(std::nullptr_t) const noexcept
    {
        return m_ptr != nullptr;
    }

    [[nodiscard]] std::size_t refCount() const noexcept
    {
        return m_ptr ? m_ptr->refCount() : 0;
    }

    void release() noexcept
    {
        if (m_ptr != nullptr && m_ptr->release()) {
            delete m_ptr;
            m_ptr = nullptr;
        }
    }

    [[nodiscard]] T* take() noexcept
    {
        return std::exchange(m_ptr, nullptr);
    }

private:
    explicit RefPtr(RefCounter<T>* ptr) noexcept
      : m_ptr(ptr)
    {}

    T* data() const noexcept
    {
        if constexpr (std::is_same_v<RefCounter<T>, T>) {
            return m_ptr;
        } else {
            return m_ptr->data();
        }
    }

    RefCounter<T>* m_ptr = nullptr;
};

template<typename T, typename... Args>
RefPtr<T> makeRefPtr(Args&&... args)
{
    return RefPtr<T>(new RefCounter<T>(std::forward<Args>(args)...));
}

struct NotAddRefTag
{};
inline constexpr auto notAddRef = NotAddRefTag();

template<typename T>
RefPtr<T> refPtrFromRawPtr(T* rawPtr)
{
    rawPtr->addRef();
    return RefPtr<T>(rawPtr);
}

template<typename T>
RefPtr<T> refPtrFromRawPtr(T* rawPtr, NotAddRefTag /*unused*/)
{
    return RefPtr<T>(rawPtr);
}

}   // namespace nhope::detail
