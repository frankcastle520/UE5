// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "uLang/Common/Common.h"
#include "uLang/Common/Algo/Cases.h"
#include "uLang/Common/Misc/Optional.h"
#include "uLang/Common/Text/UTF8StringView.h"

namespace uLang
{

// Describes the role a package plays in a Verse project
enum class EPackageRole : uint8_t
{
    Source,
    External,
    GeneralCompatConstraint,
    PersistenceCompatConstraint,
    PersistenceSoftCompatConstraint
};

constexpr auto ExternalPackageRole = Cases<
    EPackageRole::External,
    EPackageRole::GeneralCompatConstraint,
    EPackageRole::PersistenceCompatConstraint,
    EPackageRole::PersistenceSoftCompatConstraint>;

constexpr auto ConstraintPackageRole = Cases<
    EPackageRole::GeneralCompatConstraint,
    EPackageRole::PersistenceCompatConstraint,
    EPackageRole::PersistenceSoftCompatConstraint>;

static inline const char* ToString(EPackageRole Role)
{
    switch (Role)
    {
    case EPackageRole::Source: return "Source";
    case EPackageRole::External: return "External";
    case EPackageRole::GeneralCompatConstraint: return "GeneralCompatConstraint";
    case EPackageRole::PersistenceCompatConstraint: return "PersistenceCompatConstraint";
    case EPackageRole::PersistenceSoftCompatConstraint: return "PersistenceSoftCompatConstraint";
    default: ULANG_UNREACHABLE();
    }
}

static inline TOptional<EPackageRole> ToPackageRole(const CUTF8StringView& String)
{
    if (String == "Source")
    {
        return {EPackageRole::Source};
    }
    else if (String == "External")
    {
        return {EPackageRole::External};
    }
    else if (String == "GeneralCompatConstraint")
    {
        return {EPackageRole::GeneralCompatConstraint};
    }
    else if (String == "PersistenceCompatConstraint")
    {
        return {EPackageRole::PersistenceCompatConstraint};
    }
    else if (String == "PersistenceSoftCompatConstraint")
    {
        return {EPackageRole::PersistenceSoftCompatConstraint};
    }
    else
    {
        return {};
    }
}

}
