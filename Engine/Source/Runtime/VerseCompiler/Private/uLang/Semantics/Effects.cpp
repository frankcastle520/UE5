// Copyright Epic Games, Inc. All Rights Reserved.
// uLang Compiler Public API

#include "uLang/Semantics/Effects.h"

namespace
{
    uLang::SEffectInfo EffectInfos[] =
    {
#define VISIT_EFFECT(Name) { #Name, uLang::EEffect::Name },
VERSE_ENUM_EFFECTS(VISIT_EFFECT)
#undef VISIT_EFFECT
    };
}

namespace uLang
{
TRangeView<SEffectInfo*, SEffectInfo*> AllEffectInfos()
{
    return TRangeView<SEffectInfo*, SEffectInfo*>(EffectInfos, EffectInfos + ULANG_COUNTOF(EffectInfos));
}

SEffectInfo GetEffectInfo(EEffect Effect)
{
    if (size_t(Effect) >= ULANG_COUNTOF(EffectInfos))
    {
        ULANG_ERRORF("Invalid effect enum %zu", size_t(Effect));
        ULANG_UNREACHABLE();
    }
    else
    {
        return EffectInfos[size_t(Effect)];
    }
}
}