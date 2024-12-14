// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Blueprint/BlueprintExceptionInfo.h"
#include "Misc/AssertionMacros.h"

#if !WITH_VERSE_BPVM
#include "VerseVM/VVMContext.h"
#endif

namespace verse
{

#if WITH_VERSE_BPVM
struct FExecutionContextImpl
{
private:
	friend struct FExecutionContext;

	FExecutionContextImpl() = default;

	COREUOBJECT_API static FExecutionContextImpl* Claim();
	COREUOBJECT_API void Release();

	COREUOBJECT_API static FExecutionContextImpl* GetCurrent();

	bool bActive = false;
};
#endif

// A thread must have an active execution context to run Verse code.
struct FExecutionContext
{
	FExecutionContext(const FExecutionContext& Other)
		: Impl(Other.Impl)
	{
		ensure(!bBlockAllExecution);
	}

#if WITH_VERSE_BPVM
	static FExecutionContext Create()
	{
		ensure(!bBlockAllExecution);
		return FExecutionContext(FExecutionContextImpl::Claim());
	}
#endif

	static FExecutionContext GetActiveContext()
	{
		ensure(!bBlockAllExecution);
#if WITH_VERSE_BPVM
		return FExecutionContext(FExecutionContextImpl::GetCurrent());
#else
		return FExecutionContext(::Verse::FRunningContextPromise{});
#endif
	}

#if !WITH_VERSE_BPVM
	::Verse::FRunningContext GetContext()
	{
		return Impl;
	}
#endif

	static bool SetBlockExecution(bool ShouldBlock)
	{
		bool bWasBlocked = bBlockAllExecution;
		bBlockAllExecution = ShouldBlock;
		return bWasBlocked;
	}
	static bool IsExecutionBlocked() { return bBlockAllExecution; }

private:
#if WITH_VERSE_BPVM
	explicit FExecutionContext(FExecutionContextImpl* InImpl)
		: Impl(InImpl)
	{
		ensureMsgf(Impl != nullptr && Impl->bActive, TEXT("Running Verse code outside of an execution context!"));
	}
#else
	explicit FExecutionContext(::Verse::FRunningContext InImpl)
		: Impl(InImpl)
	{
	}
#endif

	COREUOBJECT_API static bool bBlockAllExecution;

#if WITH_VERSE_BPVM
	FExecutionContextImpl* Impl;
#else
	::Verse::FRunningContext Impl;
#endif
};

} // namespace verse
