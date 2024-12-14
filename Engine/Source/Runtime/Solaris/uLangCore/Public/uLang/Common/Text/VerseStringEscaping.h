// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Text/UTF8String.h"

namespace uLang
{
    class CUTF8StringView;

    namespace VerseStringEscaping
    {
        ULANGCORE_API CUTF8String EscapeString(const CUTF8StringView& StringView);
    }
}