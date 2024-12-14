// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "uLang/Common/Templates/References.h"

namespace uLang {
template <typename TFirst, typename TLast>
struct TRangeView
{
    TFirst First;
    TLast Last;

    TRangeView()
        : First()
        , Last()
    {}

    template <typename TRange>
    explicit TRangeView(TRange&& Arg)
        : TRangeView(Arg.begin(), Arg.end())
    {}

    TRangeView(TFirst InFirst, TLast InLast)
        : First(InFirst)
        , Last(InLast)
    {}

    TFirst begin() const
    {
        return First;
    }

    TLast end() const
    {
        return Last;
    }

    bool IsEmpty() const
    {
        return First == Last;
    }

    int32_t Num() const
    {
        return static_cast<int32_t>(Last - First);
    }

    template <typename TArg>
    decltype(auto) operator[](TArg&& Arg)
    {
        return First[uLang::ForwardArg<decltype(Arg)>(Arg)];
    }

    template <typename TArg>
    decltype(auto) operator[](TArg&& Arg) const
    {
        return First[uLang::ForwardArg<decltype(Arg)>(Arg)];
    }
};

template <typename T>
TRangeView(T&& Arg) -> TRangeView<decltype(Arg.begin()), decltype(Arg.end())>;

template <typename TFirst, typename TLast>
TFirst begin(TRangeView<TFirst, TLast> const& Range)
{
    return Range.begin();
}

template <typename TFirst, typename TLast>
TLast end(TRangeView<TFirst, TLast> const& Range)
{
    return Range.end();
}

template <typename T>
TRangeView<T*, T*> SingletonRangeView(T& Arg)
{
    return {&Arg, &Arg + 1};
}
}