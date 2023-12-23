#pragma once

#include <mutex>
#include <shared_mutex>
#include <type_traits>
#include <utility>

#include "nhope/utils/type.h"

namespace nhope {

template<typename T>
class LockableValue final
{
public:
    class ReadAccess;
    class WriteAccess;

    LockableValue(const LockableValue&) = delete;
    LockableValue& operator=(const LockableValue&) = delete;

    explicit LockableValue(const T& value) noexcept(std::is_nothrow_copy_constructible_v<T>)
      : m_value(value)
    {}

    explicit LockableValue(T&& value) noexcept(std::is_nothrow_move_constructible_v<T>)
      : m_value(std::move(value))
    {}

    template<typename... Args>
    explicit LockableValue(Args&&... args)
      : m_value(std::forward<Args>(args)...)
    {}

    ReadAccess readAccess() const
    {
        return ReadAccess(*this);
    }

    /*!
     * @brief Получить доступ на чтение в замыкании
     * 
     * @tparam Fn функция сигнатуры void(const T&)
     * @param f 
     */
    template<typename Fn>
    void readAccess(Fn f) const
    {
        static_assert(checkFunctionParams<Fn, const T&>() || checkFunctionParams<Fn, T>(),
                      "expect void(const T&) or void(T) signature");

        const auto ra = readAccess();
        f(*ra);
    }

    WriteAccess writeAccess()
    {
        return WriteAccess(*this);
    }

    /*!
     * @brief Получить доступ на запись в замыкании
     * 
     * @tparam Fn функция сигнатуры void(T&)
     * @param f 
     */
    template<typename Fn>
    void writeAccess(Fn f)
    {
        static_assert(checkFunctionParamsV<Fn, T&>, "expect void(T&) signature");
        auto wa = writeAccess();
        f(*wa);
    }

    LockableValue& operator=(const T& value)
    {
        WriteAccess wa(*this);
        *wa = value;
        return *this;
    }

    T copy() const
    {
        ReadAccess ra(*this);
        return T(*ra);
    }

private:
    mutable std::shared_mutex m_mutex;
    T m_value;
};

template<typename T>
class LockableValue<T>::ReadAccess final
{
public:
    explicit ReadAccess(const LockableValue& lockableValue)
      : m_lock(lockableValue.m_mutex)
      , m_value(lockableValue.m_value)
    {}

    const T* operator->() const noexcept
    {
        return &m_value;
    }
    const T& operator*() const noexcept
    {
        return m_value;
    }

private:
    std::shared_lock<std::shared_mutex> m_lock;
    const T& m_value;   // NOLINT cppcoreguidelines-avoid-const-or-ref-data-members
};

template<typename T>
class LockableValue<T>::WriteAccess final
{
public:
    explicit WriteAccess(LockableValue& lockableValue)
      : m_lock(lockableValue.m_mutex)
      , m_value(lockableValue.m_value)
    {}

    T* operator->() noexcept
    {
        return &m_value;
    }

    T& operator*() noexcept
    {
        return m_value;
    }

private:
    std::scoped_lock<std::shared_mutex> m_lock;
    T& m_value;   // NOLINT cppcoreguidelines-avoid-const-or-ref-data-members
};

template<typename T>
using ReadAccess = typename LockableValue<T>::ReadAccess;

template<typename T>
using WriteAccess = typename LockableValue<T>::WriteAccess;

}   // namespace nhope
