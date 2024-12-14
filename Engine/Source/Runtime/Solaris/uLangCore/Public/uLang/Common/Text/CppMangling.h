// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Text/UTF8String.h"

namespace uLang
{
    class CUTF8StringView;

    namespace CppMangling
    {
        ULANGCORE_API CUTF8String Mangle(const CUTF8StringView& StringView);
        ULANGCORE_API CUTF8String Demangle(const CUTF8StringView& StringView);
    }
}