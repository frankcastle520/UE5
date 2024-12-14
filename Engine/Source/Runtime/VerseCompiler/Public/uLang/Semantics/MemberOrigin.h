// Copyright Epic Games, Inc. All Rights Reserved.
// uLang Compiler Public API

#pragma once

namespace uLang
{
/// Specifies whether to find only member definitions originating in the current type, or inherited, or either.
enum class EMemberOrigin
{
    Original,
    InheritedOrOriginal,
    Inherited,
};
}