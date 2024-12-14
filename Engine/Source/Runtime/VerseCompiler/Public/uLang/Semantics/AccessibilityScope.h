// Copyright Epic Games, Inc. All Rights Reserved.
// uLang Compiler Public API

#pragma once

#include "uLang/Common/Text/UTF8String.h"
#include "uLang/Common/Containers/Array.h"

namespace uLang
{
class CDefinition;
class CScope;
class CTypeBase;
struct SAccessLevel;

struct SDigestScope
{
    bool bEpicInternal{false};
    bool bInternal{false};
};

struct SAccessibilityScope
{
    enum class EKind
    {
        Universal,
        EpicInternal,
        Scope
    };

    EKind _Kind{ EKind::Universal };
    TArray<const CScope*> _Scopes;
    const CTypeBase* _SuperType{ nullptr };

    bool IsUniversal() const       { return _Kind == SAccessibilityScope::EKind::Universal; }

    bool IsVisibleInDigest(SDigestScope DigestScope) const
    {
        switch (_Kind)
        {
        case EKind::Universal: return true;
        case EKind::EpicInternal: return DigestScope.bEpicInternal;
        case EKind::Scope: return DigestScope.bInternal;
        default:
            ULANG_UNREACHABLE();
        }
    }

    bool IsMoreAccessibleThan(const SAccessibilityScope& Other) const { return !IsSubsetOf(Other); }
    bool IsLessAccessibleThan(const SAccessibilityScope& Other) const { return Other.IsMoreAccessibleThan(*this); }

    VERSECOMPILER_API CUTF8String Describe() const;

private:
    VERSECOMPILER_API bool IsSubsetOf(const SAccessibilityScope& Other) const;
};

VERSECOMPILER_API SAccessibilityScope GetAccessibilityScope(const CDefinition& Definition, const SAccessLevel& InitialAccessLevel);
VERSECOMPILER_API SAccessibilityScope GetAccessibilityScope(const CDefinition& Definition);

} // namespace uLang