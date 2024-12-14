// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

namespace uLang {
template <auto... Args>
struct TCases
{
    template <typename U>
    constexpr friend bool operator==(const U& Left, TCases Right)
    {
        return (... || (Left == Args));
    }

    template <typename U>
    constexpr friend bool operator==(TCases Left, const U& Right)
    {
        return (... || (Args == Right));
    }

    template <typename U>
    constexpr friend bool operator!=(const U& Left, TCases Right)
    {
        return (... && (Left != Args));
    }

    template <typename U>
    constexpr friend bool operator!=(TCases Left, const U& Right)
    {
        return (... && (Args != Right));
    }
};

template <auto... Args>
constexpr TCases<Args...> Cases{};
}
