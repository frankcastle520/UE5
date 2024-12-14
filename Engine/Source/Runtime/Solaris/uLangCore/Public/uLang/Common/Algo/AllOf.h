// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Templates/Invoke.h"
#include "uLang/Common/Templates/References.h"

namespace uLang {
template <typename FirstIterator, typename LastIterator, typename Function>
bool AllOf(FirstIterator First, LastIterator Last, Function F)
{
    for (; First != Last; ++First)
    {
        if (!uLang::Invoke(F, *First))
        {
            return false;
        }
    }
    return true;
}

template <typename Range, typename Function>
bool AllOf(Range&& R, Function&& F)
{
    return uLang::AllOf(R.begin(), R.end(), uLang::ForwardArg<Function>(F));
}
}
