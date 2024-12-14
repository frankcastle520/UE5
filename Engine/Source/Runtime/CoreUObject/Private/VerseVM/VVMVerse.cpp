// Copyright Epic Games, Inc. All Rights Reserved.

#include "VerseVM/VVMVerse.h"
#include "AutoRTFM/AutoRTFM.h"
#include "UObject/VerseValueProperty.h"
#include "VerseVM/VVMEmergentTypeCreator.h"
#include "VerseVM/VVMFalse.h"
#include "VerseVM/VVMFrame.h"
#include "VerseVM/VVMGlobalProgram.h"
#include "VerseVM/VVMHeap.h"
#include "VerseVM/VVMTask.h"

namespace Verse
{

namespace Private
{
IEngineEnvironment* GEngineEnvironment = nullptr;
}

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
void VerseVM::Startup()
{
	Verse::FHeap::Initialize();
	Verse::FRunningContext Context = Verse::FRunningContextPromise{};

	Verse::VEmergentTypeCreator::Initialize(Context);
	Verse::VFalse::InitializeGlobals(Context);
	Verse::VTask::BindStructTrivial(Context);
	Verse::VFrame::InitializeGlobalEmpty(Context);

	// VerseVM requires RTFM enabled
#if UE_AUTORTFM || defined(__INTELLISENSE__)
	AutoRTFM::ForTheRuntime::SetAutoRTFMRuntime(AutoRTFM::ForTheRuntime::EAutoRTFMEnabledState::AutoRTFM_Enabled);
#endif

	// Register our property types
	FVValueProperty::StaticClass();
	FVRestValueProperty::StaticClass();

	if (!Verse::GlobalProgram)
	{
		GlobalProgram.Set(Context, &VProgram::New(Context, 32));
	}
}

void VerseVM::Shutdown()
{
	FHeap::Deinitialize();
}
#endif // WITH_VERSE_VM || defined(__INTELLISENSE__)

IEngineEnvironment* VerseVM::GetEngineEnvironment()
{
	return Private::GEngineEnvironment;
}

void VerseVM::SetEngineEnvironment(IEngineEnvironment* Environment)
{
	ensure(Environment == nullptr || Private::GEngineEnvironment == nullptr);
	Private::GEngineEnvironment = Environment;
}

} // namespace Verse
