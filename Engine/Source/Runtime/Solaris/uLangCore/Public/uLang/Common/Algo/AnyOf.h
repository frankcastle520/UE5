// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Templates/Invoke.h"
#include "uLang/Common/Templates/References.h"

namespace uLang {
template <typename FirstIterator, typename LastIterator, typename Function>
bool AnyOf(FirstIterator First, LastIterator Last, Function F)
{
    for (; First != Last; ++First)
    {
        if (uLang::Invoke(F, *First))
        {
            return true;
        }
    }
    return false;
}

template <typename Range, typename Function>
bool AnyOf(Range&& R, Function&& F)
{
    return uLang::AnyOf(R.begin(), R.end(), uLang::ForwardArg<Function>(F));
}
}
