// Copyright Epic Games, Inc. All Rights Reserved.

#include "FieldNotificationRewindDebugger.h"

#include "FieldNotificationTraceProvider.h"
//#include "IGameplayProvider.h"

namespace UE::FieldNotification
{

void FRewindDebugger::Update(float DeltaTime, IRewindDebugger* RewindDebugger)
{
	const TraceServices::IAnalysisSession* AnalysisSession = RewindDebugger->GetAnalysisSession();
	TraceServices::FAnalysisSessionReadScope SessionReadScope(*AnalysisSession);
	
	const FTraceProvider* TraceProvider = AnalysisSession->ReadProvider<FTraceProvider>(FTraceProvider::ProviderName);
	//const IGameplayProvider* GameplayProvider = AnalysisSession->ReadProvider<IGameplayProvider>("GameplayProvider");
	
	const TraceServices::IFrameProvider& FrameProvider = TraceServices::ReadFrameProvider(*RewindDebugger->GetAnalysisSession());
	TraceServices::FFrame Frame;
	if (FrameProvider.GetFrameFromTime(ETraceFrameType::TraceFrameType_Game, RewindDebugger->CurrentTraceTime(), Frame))
	{
		// each tick update the UMG Preview Window with what we are trying to debug
	}
}

void FRewindDebugger::RecordingStarted(IRewindDebugger*)
{
	UE::Trace::ToggleChannel(TEXT("FieldNotificationChannel"), true);
}

void FRewindDebugger::RecordingStopped(IRewindDebugger*)
{
	UE::Trace::ToggleChannel(TEXT("FieldNotificationChannel"), false);
}

} // namespace
