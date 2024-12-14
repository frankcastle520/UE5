// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Templates/References.h"

namespace uLang {
template <typename T>
struct TValueIterator
{
    TValueIterator& operator++()
    {
        ++_Value;
        return *this;
    }

    TValueIterator operator++(int)
    {
        TValueIterator result = *this;
        ++* this;
        return result;
    }

    T const& operator*() const
    {
        return _Value;
    }

    friend bool operator==(const TValueIterator& Left, const TValueIterator& Right)
    {
        return Left._Value == Right._Value;
    }

    friend bool operator!=(const TValueIterator& Left, const TValueIterator& Right)
    {
        return Left._Value != Right._Value;
    }

    T _Value;
};

template <typename T>
TValueIterator(T&&) -> TValueIterator<TDecayT<T>>;

template <typename T>
struct TUntil
{
    TValueIterator<T> begin() const
    {
        return TValueIterator{0};
    }

    TValueIterator<T> end() const
    {
        return TValueIterator{_Last};
    }

    T _Last;
};

template <typename T>
TUntil(T&&) -> TUntil<TDecayT<T>>;
}
