// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VVMClass.h"
#include "VVMGlobalTrivialEmergentTypePtr.h"
#include "VVMWriteBarrier.h"

namespace Verse
{
/// The lexical scope of a function.
/// In the future when we support lambdas this will also contain the captures for them.
struct VScope : VCell
{
	DECLARE_DERIVED_VCPPCLASSINFO(COREUOBJECT_API, VCell);
	COREUOBJECT_API static TGlobalTrivialEmergentTypePtr<&StaticCppClassInfo> GlobalTrivialEmergentType;

	COREUOBJECT_API static VScope& New(FAllocationContext Context, VClass* InSuperClass = nullptr);

	// This is just a `VClass` because you can only refer to classes using `(super:)` currently.
	TWriteBarrier<VClass> SuperClass;

private:
	VScope(FAllocationContext Context, VClass* InSuperClass);
};
} // namespace Verse
#endif // WITH_VERSE_VM
