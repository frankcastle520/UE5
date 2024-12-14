// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Templates/Invoke.h"

namespace uLang {
template <typename TFirst, typename TLast, typename T>
TFirst Find(TFirst First, TLast Last, T&& Arg)
{
    for (; First != Last; ++First)
    {
        if (*First == Arg)
        {
            break;
        }
    }
    return First;
}
}
