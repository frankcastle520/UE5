// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "uLang/Common/Common.h"
#include "uLang/Common/Misc/Optional.h"
#include "uLang/Common/Text/UTF8StringView.h"

namespace uLang
{

// Describes the origin and visibility of Verse code
// This mirrors EVerseScope in VerseScope.h in the Projects module.
enum class EVerseScope : uint8_t
{
    PublicAPI,    // Created by Epic and only public definitions will be visible to public users
    InternalAPI,  // Created by Epic and is entirely hidden from public users
    PublicUser,   // Created by a public user
    InternalUser, // Created by an Epic internal user
};

static inline bool IsPublicScope(EVerseScope VerseScope)
{
    switch (VerseScope)
    {
    case EVerseScope::PublicAPI:
    case EVerseScope::PublicUser: return true;
    case EVerseScope::InternalAPI:
    case EVerseScope::InternalUser: return false;
    default: ULANG_UNREACHABLE();
    }
}

static inline const char* ToString(EVerseScope VerseScope)
{
    switch (VerseScope)
    {
    case EVerseScope::PublicAPI: return "PublicAPI";
    case EVerseScope::InternalAPI: return "InternalAPI";
    case EVerseScope::PublicUser: return "PublicUser";
    case EVerseScope::InternalUser: return "InternalUser";
    default: ULANG_UNREACHABLE();
    }
}

static inline TOptional<EVerseScope> ToVerseScope(const CUTF8StringView& String)
{
    if (String == "PublicAPI")
    {
        return { EVerseScope::PublicAPI };
    }
    else if (String == "InternalAPI")
    {
        return { EVerseScope::InternalAPI };
    }
    else if (String == "PublicUser")
    {
        return { EVerseScope::PublicUser };
    }
    else if (String == "InternalUser")
    {
        return { EVerseScope::InternalUser };
    }
    else
    {
        return {};
    }
}

}
