// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VerseVM/VVMContext.h"
#include "VerseVM/VVMLocation.h"
#include "VerseVM/VVMUniqueString.h"
#include "VerseVM/VVMValue.h"

namespace Verse
{
struct FLocation;
struct FOp;
struct VFrame;
struct VUniqueString;

struct FDebugger
{
	virtual ~FDebugger() = default;
	virtual void Notify(FRunningContext, VFrame&, const FOp&) = 0;
	virtual void AddLocation(FAllocationContext, VUniqueString& FilePath, const FLocation&) = 0;
};

COREUOBJECT_API FDebugger* GetDebugger();

COREUOBJECT_API void SetDebugger(FDebugger*);

namespace Debugger
{
using FRegisters = TArray<TTuple<TWriteBarrier<VUniqueString>, VValue>>;

struct FFrame
{
	explicit FFrame(FAccessContext Context, VUniqueString& Name, VUniqueString& FilePath, FRegisters Registers)
		: Name{Context, &Name}
		, FilePath{Context, &FilePath}
		, Registers{::MoveTemp(Registers)}
	{
	}

	TWriteBarrier<VUniqueString> Name;
	TWriteBarrier<VUniqueString> FilePath;
	FRegisters Registers;
};

template <typename TVisitor>
void Visit(TVisitor& Visitor, FFrame& Frame, const TCHAR* ElementName)
{
	Visit(Visitor, Frame.Name, TEXT("Name"));
	Visit(Visitor, Frame.FilePath, TEXT("FilePath"));
	for (auto&& [Name, Value] : Frame.Registers)
	{
		Visit(Visitor, Name, TEXT("RegisterName"));
		Visit(Visitor, Value, TEXT("RegisterValue"));
	}
}

COREUOBJECT_API void ForEachStackFrame(FRunningContext, VFrame&, const FOp&, TFunctionRef<void(FFrame, const FLocation*)>);
} // namespace Debugger
} // namespace Verse

#endif
