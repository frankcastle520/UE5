// Copyright Epic Games, Inc. All Rights Reserved.

#include "uLang/Semantics/AccessLevel.h"
#include "uLang/Common/Text/UTF8StringBuilder.h"
#include "uLang/Semantics/SemanticScope.h"

namespace uLang
{
CUTF8String SAccessLevel::AsCode() const
{
    CUTF8StringBuilder Builder;
    const char* LevelString = SAccessLevel::KindAsCString(_Kind);
    Builder.Append(LevelString);

    if (_Kind == SAccessLevel::EKind::Scoped)
    {
        Builder.Append("{");
        const char* Seperator = "";
        for (const CScope* Scope : _Scopes)
        {
            Builder.AppendFormat("%s%s", Seperator, Scope->GetScopePath('/', CScope::EPathMode::PrefixSeparator).AsCString());
            Seperator = ", ";
        }

        Builder.Append("}");
    }

    return Builder.MoveToString();
}
}
