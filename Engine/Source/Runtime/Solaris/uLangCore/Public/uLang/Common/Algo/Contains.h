// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Algo/Find.h"
#include "uLang/Common/Templates/References.h"

namespace uLang {
template <typename TFirst, typename TLast, typename T>
bool Contains(TFirst First, TLast Last, T&& Arg)
{
    return Find(First, Last, uLang::ForwardArg<T>(Arg)) != Last;
}

template <typename TRange, typename T>
bool Contains(TRange&& Range, T&& Arg)
{
    return Contains(Range.begin(), Range.end(), uLang::ForwardArg<T>(Arg));
}
}
