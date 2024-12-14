// Copyright Epic Games, Inc. All Rights Reserved.
// uLang Compiler Public API

#pragma once

#include "uLang/Common/Containers/Array.h"
#include "uLang/Common/Memory/Allocator.h"

namespace uLang
{
class CDefinition;
using SmallDefinitionArray = TArrayG<CDefinition*, TInlineElementAllocator<1>>;
}