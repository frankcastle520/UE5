// Copyright Epic Games, Inc. All Rights Reserved.

#include "uLang/Diagnostics/Diagnostics.h"

namespace uLang
{
int32_t CDiagnostics::GetWarningNum() const
{
    int32_t WarningNum = 0;

    for (const SGlitch* Glitch : _Glitches)
    {
        WarningNum += int32_t(Glitch->_Result.IsWarning());
    }

    return WarningNum;
}

int32_t CDiagnostics::GetErrorNum() const
{
    int32_t ErrorNum = 0;

    for (const SGlitch* Glitch : _Glitches)
    {
        ErrorNum += int32_t(Glitch->_Result.IsError());
    }

    return ErrorNum;
}
}
