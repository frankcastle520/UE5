// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VerseVM/VVMDebugger.h"

#include "VerseVM/VVMFalse.h"
#include "VerseVM/VVMFrame.h"

static Verse::FDebugger* GDebugger = nullptr;

Verse::FDebugger* Verse::GetDebugger()
{
	return GDebugger;
}

void Verse::SetDebugger(FDebugger* Arg)
{
	GDebugger = Arg;
}

namespace Verse
{
namespace
{
bool IsFalse(VValue Arg)
{
	return Arg.IsCell() && &Arg.AsCell() == GlobalFalsePtr.Get();
}
} // namespace

void Debugger::ForEachStackFrame(FRunningContext Context, VFrame& Frame, const FOp& Op, TFunctionRef<void(FFrame, const FLocation*)> F)
{
	const FOp* J = &Op;
	TWriteBarrier<VUniqueString> SelfName{Context, VUniqueString::New(Context, "Self")};
	for (VFrame* I = &Frame; I; I = I->CallerFrame.Get())
	{
		VUniqueString& FilePath = *I->Procedure->FilePath;
		if (FilePath.Num() == 0)
		{
			continue;
		}
		TArray<TTuple<TWriteBarrier<VUniqueString>, VValue>> Registers;
		VValue SelfValue = I->Registers[FRegisterIndex::SELF].Get(Context);
		if (IsFalse(SelfValue))
		{
			Registers.Reserve(I->Procedure->NumRegisterNames);
		}
		else
		{
			Registers.Reserve(I->Procedure->NumRegisterNames + 1);
			Registers.Emplace(SelfName, I->Registers[FRegisterIndex::SELF].Get(Context));
		}
		for (auto K = I->Procedure->GetRegisterNamesBegin(), Last = I->Procedure->GetRegisterNamesEnd(); K != Last; ++K)
		{
			Registers.Emplace(K->Name, I->Registers[K->Index.Index].Get(Context));
		}
		FFrame DebuggerFrame{Context, *I->Procedure->Name, FilePath, ::MoveTemp(Registers)};
		const FLocation* Location = I->Procedure->GetLocation(*J);
		F(::MoveTemp(DebuggerFrame), Location);
		J = I->CallerPC;
	}
}
} // namespace Verse

#endif
