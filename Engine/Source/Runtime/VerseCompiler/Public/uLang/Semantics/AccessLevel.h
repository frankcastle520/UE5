// Copyright Epic Games, Inc. All Rights Reserved.
// uLang Compiler Public API

#pragma once

#include "uLang/Common/Text/UTF8String.h"
#include "uLang/Common/Containers/Array.h"

namespace uLang 
{
    class CScope;

    // Mostly a wrapper around SAccessLevel::EKind with the addition of an optional list of modules for the 'scoped' access level
    struct SAccessLevel
    {
        enum class EKind : int8_t
        {
            // Unrestricted access
            Public,
            // Access limited to current module (default)   
            Internal,
            // Access limited to current class and any subclasses
            Protected,
            // Access limited to current class
            Private,
            // Access is internal to a code-specified list of module references
            Scoped,
            // Access limited to Epic code.  This is a temporary access level to be
            // replaced by `scoped` access
            EpicInternal,
        };

        EKind _Kind;

        // Only used when _Kind == SAccessLevel::EKind::Scoped
        // Access is considered internal to all listed scopes.
        uLang::TArray<const CScope*> _Scopes;

        SAccessLevel()
            : _Kind(SAccessLevel::EKind::Internal)
        {}
        SAccessLevel(SAccessLevel::EKind Kind)
            : _Kind(Kind)
        {}
        SAccessLevel(const SAccessLevel&) = default;
        SAccessLevel& operator=(const SAccessLevel&) = default;

        friend bool operator!=(const SAccessLevel& Lhs, const SAccessLevel& Rhs)
        {
            return Lhs._Kind != Rhs._Kind
                || Lhs._Scopes != Rhs._Scopes;
        }

        VERSECOMPILER_API CUTF8String AsCode() const;

        static const char* KindAsCString(SAccessLevel::EKind AccessLevelKind)
        {
            switch (AccessLevelKind)
            {
            case SAccessLevel::EKind::Public: return "public";
            case SAccessLevel::EKind::Internal: return "internal";
            case SAccessLevel::EKind::Protected: return "protected";
            case SAccessLevel::EKind::Private: return "private";
            case SAccessLevel::EKind::Scoped: return "scoped";
            case SAccessLevel::EKind::EpicInternal: return "epic_internal";
            default: ULANG_UNREACHABLE();
            }
        }
    };
}
