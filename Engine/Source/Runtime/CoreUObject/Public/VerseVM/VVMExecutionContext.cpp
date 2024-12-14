// Copyright Epic Games, Inc. All Rights Reserved.

#include "VerseVM/VVMExecutionContext.h"

namespace verse
{
#if WITH_VERSE_BPVM
thread_local TUniquePtr<FExecutionContextImpl> CurrentImpl;

FExecutionContextImpl* FExecutionContextImpl::Claim()
{
	FExecutionContextImpl* Result = CurrentImpl.Get();
	if (!Result)
	{
		Result = new FExecutionContextImpl;
		CurrentImpl.Reset(Result);
	}

	ensureMsgf(!Result->bActive, TEXT("Creating a new Verse execution context when one is already active!"));
	Result->bActive = true;

	return Result;
}

void FExecutionContextImpl::Release()
{
	ensure(bActive);
	ensure(CurrentImpl.Get() == this);
	bActive = false;
}

FExecutionContextImpl* FExecutionContextImpl::GetCurrent()
{
	return CurrentImpl.Get();
}
#endif

COREUOBJECT_API bool FExecutionContext::bBlockAllExecution = false;
} // namespace verse
