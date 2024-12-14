// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "Containers/ArrayView.h"
#include "VVMFalse.h"
#include "VVMGlobalTrivialEmergentTypePtr.h"
#include "VVMScope.h"
#include "VVMType.h"

namespace Verse
{
struct FOpResult;
struct VPackage;
struct VTask;

using FNativeCallResult = FOpResult;

// A function that is implemented in C++
struct VNativeFunction : VHeapValue
{
	DECLARE_DERIVED_VCPPCLASSINFO(COREUOBJECT_API, VHeapValue);
	COREUOBJECT_API static TGlobalTrivialEmergentTypePtr<&StaticCppClassInfo> GlobalTrivialEmergentType;

	const uint32 NumParameters;

	// Interface between VerseVM and C++
	using Args = TArrayView<VValue>;
	using FThunkFn = FNativeCallResult (*)(FRunningContext, VValue Scope, Args Arguments);

	// The C++ function to call
	FThunkFn Thunk;

	TWriteBarrier<VValue> Self;

	static VNativeFunction& New(FAllocationContext Context, uint32 NumParameters, FThunkFn Thunk, VValue InSelf)
	{
		return *new (Context.AllocateFastCell(sizeof(VNativeFunction))) VNativeFunction(Context, NumParameters, Thunk, InSelf);
	}

	static VNativeFunction& NewUnbound(FAllocationContext Context, uint32 NumParameters, FThunkFn Thunk)
	{
		return *new (Context.AllocateFastCell(sizeof(VNativeFunction))) VNativeFunction(Context, NumParameters, Thunk, VValue());
	}

	VNativeFunction& Bind(FAllocationContext Context, VValue InSelf)
	{
		checkf(!HasSelf(), TEXT("Attempting to bind `Self` to a `VNativeFunction` that already has it set; this is probably a mistake in the code generation."));
		return *new (Context.AllocateFastCell(sizeof(VNativeFunction))) VNativeFunction(Context, NumParameters, Thunk, InSelf);
	}

	// Lookup a native function and set it's thunk to a C++ function
	static COREUOBJECT_API void SetThunk(Verse::VPackage* Package, FUtf8StringView VerseScopePath, FUtf8StringView DecoratedName, FThunkFn NativeFuncPtr);

	COREUOBJECT_API bool HasSelf() const;

private:
	VNativeFunction(FAllocationContext Context, uint32 InNumParameters, FThunkFn InThunk, VValue InSelf)
		: VHeapValue(Context, &GlobalTrivialEmergentType.Get(Context))
		, NumParameters(InNumParameters)
		, Thunk(InThunk)
		, Self(Context, InSelf)
	{
	}
};

} // namespace Verse
#endif // WITH_VERSE_VM
