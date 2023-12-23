#pragma once

#include <cstddef>
#include <functional>
#include <type_traits>
#include <utility>

namespace nhope {

template<std::size_t index, typename... Types>
struct Extract
{
private:
    template<std::size_t n, typename Head, typename... Tail>
    struct Impl
    {
        using Type = typename Impl<n - 1, Tail...>::Type;
    };

    template<typename Head, typename... Tail>
    struct Impl<0, Head, Tail...>
    {
        using Type = Head;
    };

public:
    static_assert(index < sizeof...(Types), "Index out of bounds");

    using Type = typename Impl<index, Types...>::Type;
};

template<typename Type, class>
struct EnableFunc : std::false_type
{};

template<typename Type>
struct EnableFunc<Type, decltype(std::function(std::declval<Type>()), void())> : std::true_type
{};

template<typename Type>
struct FuncEnabler : public EnableFunc<std::decay_t<Type>, void>
{};

template<typename Type>
constexpr bool isFunctional()
{
    if constexpr (std::is_member_function_pointer_v<Type>) {
        return true;
    } else {
        return FuncEnabler<Type>::value;
    }
}

template<typename R, typename... Args>
struct FunctionProps
{
    static constexpr std::size_t argumentCount = sizeof...(Args);
    static constexpr bool hasSingleArgument = argumentCount == 1;

    using ReturnType = R;

    template<std::size_t index>
    using ArgumentType = typename Extract<index, Args...>::Type;
};

template<typename R, typename... Args>
struct FunctionProps<std::function<R(Args...)>>
{
    static constexpr std::size_t argumentCount = sizeof...(Args);
    static constexpr bool hasSingleArgument = argumentCount == 1;

    using ReturnType = R;

    template<std::size_t index>
    using ArgumentType = typename Extract<index, Args...>::Type;
};

template<typename FunctionProps, template<typename> typename cond, int pos = 0>
constexpr int findArgument()
{
    if constexpr (pos >= FunctionProps::argumentCount) {
        return -1;
    } else if constexpr (cond<typename FunctionProps::template ArgumentType<pos>>::value) {
        return pos;
    } else {
        return findArgument<FunctionProps, cond, pos + 1>();
    }
}

template<typename Fn>
constexpr bool hasSingleArgument()
{
    using FnProps = FunctionProps<decltype(std::function(std::declval<Fn>()))>;
    return FnProps::hasSingleArgument;
}

template<typename Fn, typename... Args, std::size_t... I>
constexpr bool checkFunctionArgumentTypes(std::index_sequence<I...> /*unused*/)
{
    using FnProps = FunctionProps<decltype(std::function(std::declval<Fn>()))>;
    return (std::is_same_v<typename Extract<I, Args...>::Type, typename FnProps::template ArgumentType<I>> && ...);
}

template<typename Fn, typename... Args>
constexpr bool checkFunctionParams()
{
    if constexpr (isFunctional<Fn>()) {
        using FnProps = FunctionProps<decltype(std::function(std::declval<Fn>()))>;
        if constexpr (FnProps::argumentCount != sizeof...(Args)) {
            return false;
        } else {
            return checkFunctionArgumentTypes<Fn, Args...>(std::make_index_sequence<FnProps::argumentCount>{});
        }
    }
    return false;
}

template<typename Fn, typename... Args>
static constexpr bool checkFunctionParamsV = checkFunctionParams<Fn, Args...>();

template<typename Fn, typename ReturnType>
constexpr bool checkReturnType()
{
    if constexpr (isFunctional<Fn>()) {
        using FnProps = FunctionProps<decltype(std::function(std::declval<Fn>()))>;
        return std::is_same_v<typename FnProps::ReturnType, ReturnType>;
    }
    return false;
}

template<typename Fn, typename ReturnType>
static constexpr bool checkReturnTypeV = checkReturnType<Fn, ReturnType>();

template<typename Fn, typename R, typename... Args>
constexpr bool checkFunctionSignature()
{
    return checkReturnTypeV<Fn, R> && checkFunctionParamsV<Fn, Args...>;
}

template<typename Fn, typename R, typename... Args>
static constexpr bool checkFunctionSignatureV = checkFunctionSignature<Fn, R, Args...>();

template<typename T, typename U, class>
struct HasEqualImpl : std::false_type
{};

template<typename T, typename U>
struct HasEqualImpl<T, U, decltype(std::declval<T>() == std::declval<U>(), void())> : std::true_type
{};

template<typename T, typename U>
struct HasEqual : HasEqualImpl<T, U, void>
{};

}   // namespace nhope
