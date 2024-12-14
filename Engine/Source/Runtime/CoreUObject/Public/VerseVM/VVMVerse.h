// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace Verse
{
class IEngineEnvironment;

class VerseVM
{
public:
#if WITH_VERSE_VM || defined(__INTELLISENSE__)
	COREUOBJECT_API static void Startup();
	COREUOBJECT_API static void Shutdown();
#endif // WITH_VERSE_VM
	COREUOBJECT_API static IEngineEnvironment* GetEngineEnvironment();
	COREUOBJECT_API static void SetEngineEnvironment(IEngineEnvironment* Environment);
};
} // namespace Verse
