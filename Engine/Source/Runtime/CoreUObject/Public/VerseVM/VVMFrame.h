// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VerseVM/VVMEmergentTypeCreator.h"
#include "VerseVM/VVMGlobalTrivialEmergentTypePtr.h"
#include "VerseVM/VVMProcedure.h"
#include "VerseVM/VVMRestValue.h"
#include "VerseVM/VVMReturnSlot.h"
#include "VerseVM/VVMType.h"

namespace Verse
{
struct FOp;
struct VProcedure;

struct VFrame : VCell
{
	DECLARE_DERIVED_VCPPCLASSINFO(COREUOBJECT_API, VCell);
	COREUOBJECT_API static TGlobalTrivialEmergentTypePtr<&StaticCppClassInfo> GlobalTrivialEmergentType;

	FOp* CallerPC{nullptr};
	TWriteBarrier<VFrame> CallerFrame;
	VReturnSlot ReturnSlot;

	TWriteBarrier<VProcedure> Procedure;
	const uint32 NumRegisters;
	VRestValue Registers[];

	static TGlobalHeapPtr<VFrame> GlobalEmptyFrame;

	template <typename ReturnSlotType>
	static VFrame& New(FAllocationContext Context, FOp* CallerPC, VFrame* CallerFrame, ReturnSlotType ReturnSlot, VProcedure& Procedure)
	{
		uint32 NumRegisters = Procedure.NumRegisters;
		return *new (Context.AllocateFastCell(offsetof(VFrame, Registers) + sizeof(VRestValue) * NumRegisters)) VFrame(Context, CallerPC, CallerFrame, ReturnSlot, Procedure);
	}

	VFrame& CloneWithoutCallerInfo(FAllocationContext Context)
	{
		return VFrame::New(Context, *this);
	}

	static void InitializeGlobalEmpty(FAllocationContext Context);

private:
	static VFrame& New(FAllocationContext Context, VFrame& Other)
	{
		return *new (Context.AllocateFastCell(offsetof(VFrame, Registers) + sizeof(VRestValue) * Other.NumRegisters)) VFrame(Context, Other);
	}

	template <typename ReturnSlotType>
	VFrame(FAllocationContext Context, FOp* CallerPC, VFrame* CallerFrame, ReturnSlotType ReturnSlot, VProcedure& Procedure)
		: VCell(Context, &GlobalTrivialEmergentType.Get(Context))
		, CallerPC(CallerPC)
		, CallerFrame(Context, CallerFrame)
		, ReturnSlot(Context, ReturnSlot)
		, Procedure(Context, Procedure)
		, NumRegisters(Procedure.NumRegisters)
	{
		for (uint32 RegisterIndex = 0; RegisterIndex < NumRegisters; ++RegisterIndex)
		{
			// TODO SOL-4222: Pipe through proper split depth here.
			new (&Registers[RegisterIndex]) VRestValue(0);
		}
	}

	// We don't copy the CallerFrame/CallerPC because during lenient execution
	// this won't return to the caller.
	VFrame(FAllocationContext Context, VFrame& Other)
		: VCell(Context, &GlobalTrivialEmergentType.Get(Context))
		, ReturnSlot(Context, Other.ReturnSlot.Get(Context))
		, Procedure(Context, Other.Procedure.Get())
		, NumRegisters(Other.NumRegisters)
	{
		ReturnSlot.EffectToken.Set(Context, Other.ReturnSlot.EffectToken.Get(Context));
		for (uint32 RegisterIndex = 0; RegisterIndex < NumRegisters; ++RegisterIndex)
		{
			// TODO SOL-4222: Pipe through proper split depth here.
			new (&Registers[RegisterIndex]) VRestValue(0);
			Registers[RegisterIndex].Set(Context, Other.Registers[RegisterIndex].Get(Context));
		}
	}
};

} // namespace Verse

#endif
