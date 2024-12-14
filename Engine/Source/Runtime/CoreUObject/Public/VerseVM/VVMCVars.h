// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "HAL/IConsoleManager.h"
#include "Math/RandomStream.h"

namespace Verse
{
extern COREUOBJECT_API TAutoConsoleVariable<bool> CVarTraceExecution;
extern COREUOBJECT_API TAutoConsoleVariable<bool> CVarSingleStepTraceExecution;
extern COREUOBJECT_API TAutoConsoleVariable<bool> CVarDumpBytecode;
extern COREUOBJECT_API TAutoConsoleVariable<float> CVarUObjectProbability;
extern COREUOBJECT_API FRandomStream RandomUObjectProbability;
} // namespace Verse

#endif // WITH_VERSE_VM
