// Copyright Epic Games, Inc. All Rights Reserved.

#include "uLang/Semantics/AccessibilityScope.h"
#include "uLang/Common/Text/UTF8StringBuilder.h"
#include "uLang/Semantics/AccessLevel.h"
#include "uLang/Semantics/Definition.h"
#include "uLang/Semantics/SemanticProgram.h"
#include "uLang/Semantics/SemanticScope.h"
#include "uLang/Semantics/SemanticTypes.h"

namespace uLang
{

CUTF8String SAccessibilityScope::Describe() const
{
    CUTF8StringBuilder Result;
    if (_SuperType)
    {
        Result.AppendFormat("from subtypes of %s, ", _SuperType->AsCode().AsCString());
    }
    // Expected context is: accessible <result of this function>
    switch (_Kind)
    {
    case EKind::Universal: Result.Append("universally"); break;
    case EKind::EpicInternal: Result.Append("from any Epic-internal path"); break;
    case EKind::Scope:
    {
        if (_Scopes.IsEmpty())
        {
            Result.Append("from nowhere");
        }
        else
        {
            Result.Append("from subpaths of ");
            const char* Separator = "";
            for (const CScope* Scope : _Scopes)
            {
                Result.AppendFormat("%s%s", Separator, Scope->GetScopePath('/', CScope::EPathMode::PrefixSeparator).AsCString());
                Separator = ", ";
            }
        }
    }
    break;
    default:
        ULANG_UNREACHABLE();
    };

    return Result.MoveToString();
}

bool SAccessibilityScope::IsSubsetOf(const SAccessibilityScope& Other) const
{
    if (Other._SuperType)
    {
        if (!_SuperType || SemanticTypeUtils::IsSubtype(_SuperType, Other._SuperType))
        {
            return false;
        }
    }

    switch (Other._Kind)
    {
    case SAccessibilityScope::EKind::Universal:
    {
        return true;
    }
    case SAccessibilityScope::EKind::EpicInternal:
    {
        switch (_Kind)
        {
        case SAccessibilityScope::EKind::Universal:
        {
            return false;
        }
        case SAccessibilityScope::EKind::EpicInternal:
        {
            return true;
        }
        case SAccessibilityScope::EKind::Scope:
        {
            for (const CScope* LhsScope : _Scopes)
            {
                if (!LhsScope->IsAuthoredByEpic())
                {
                    return false;
                }
            }
            return true;
        }
        default: ULANG_UNREACHABLE();
        }
    }
    case SAccessibilityScope::EKind::Scope:
    {
        // Note: We assume here that Other can never be such set of scopes
        // that fully contains all universal and/or epic_internal scopes
        if (_Kind != SAccessibilityScope::EKind::Scope)
        {
            return false;
        }

        for (const CScope* LhsScope : _Scopes)
        {
            if (!Other._Scopes.ContainsByPredicate([LhsScope](const CScope* RhsScope) { return LhsScope->IsSameOrChildOf(RhsScope); }))
            {
                return false;
            }
        }

        return true;
    }
    default: ULANG_UNREACHABLE();
    }
}

SAccessibilityScope GetAccessibilityScope(const CDefinition& Definition, const SAccessLevel& InitialAccessLevel)
{
    SAccessibilityScope Result;

    auto ConstrainByAccessLevel = [&Result](const SAccessLevel& AccessLevel, const CScope& EnclosingScope)
        {
            if (AccessLevel._Kind == SAccessLevel::EKind::Protected)
            {
                ULANG_ASSERT(!Result._SuperType);
                Result._SuperType = EnclosingScope.ScopeAsType();
                ULANG_ASSERT(Result._SuperType);
            }

            // Use the inner-most definition in the path to the root scope that is internal or private
            // to Result._Scope to the largest "fully accessible scope" for the definition.
            if (Result._Scopes.IsEmpty())
            {
                if (AccessLevel._Kind == SAccessLevel::EKind::Private)
                {
                    Result._Scopes.Add(&EnclosingScope);
                }
                else if (AccessLevel._Kind == SAccessLevel::EKind::Internal)
                {
                    Result._Scopes.Add(EnclosingScope.GetModule());
                }
                else if (AccessLevel._Kind == SAccessLevel::EKind::Scoped)
                {
                    Result._Scopes.Add(EnclosingScope.GetModule());
                    for (const CScope* ModuleScope : AccessLevel._Scopes)
                    {
                        Result._Scopes.AddUnique(ModuleScope);
                    }
                }
                else if (AccessLevel._Kind == SAccessLevel::EKind::EpicInternal)
                {
                    // Remember that we encountered an EpicInternal scope at some point up to the first scope
                    Result._Kind = SAccessibilityScope::EKind::EpicInternal;
                }
            }
        };

    // Constrain the accessibility by the initial access level.
    ConstrainByAccessLevel(InitialAccessLevel, Definition._EnclosingScope);

    for (const CDefinition* DefinitionIt = &Definition; DefinitionIt; DefinitionIt = DefinitionIt->GetEnclosingDefinition())
    {
        // Constrain the accessibility by the definition's access level.
        const CDefinition& ConstrainingDefinition = DefinitionIt->GetDefinitionAccessibilityRoot();
        ConstrainByAccessLevel(ConstrainingDefinition.DerivedAccessLevel(), ConstrainingDefinition._EnclosingScope.GetLogicalScope());
    }

    if (!Result._Scopes.IsEmpty())
    {
        // Is this symbol accessible only from epic_internal scopes?
        if (Result._Kind == SAccessibilityScope::EKind::EpicInternal)
        {
            // Yes, remove all scopes that are not in an epic_internal scope
            Result._Scopes.RemoveAll([](const CScope* Scope) { return !Scope->IsAuthoredByEpic(); });
            // If no scopes are left at this point it means the definition is entirely inaccessible
        }
        Result._Kind = SAccessibilityScope::EKind::Scope;
    }

    return Result;
}

SAccessibilityScope GetAccessibilityScope(const CDefinition& Definition)
{
    return GetAccessibilityScope(Definition, SAccessLevel::EKind::Public);
}

} // namespace uLang
