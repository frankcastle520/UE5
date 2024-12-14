// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Templates/Invoke.h"

namespace uLang {
template <typename TFirst, typename TLast, typename TFunction>
TFirst FindIf(TFirst First, TLast Last, TFunction Function)
{
    for (; First != Last; ++First)
    {
        if (uLang::Invoke(Function, *First))
        {
            break;
        }
    }
    return First;
}
}
