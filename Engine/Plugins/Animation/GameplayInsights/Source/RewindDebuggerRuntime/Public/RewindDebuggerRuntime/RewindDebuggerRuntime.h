// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ProfilingDebugging/TraceAuxiliary.h" // for FTraceAuxiliary::EConnectionType

REWINDDEBUGGERRUNTIME_API DECLARE_LOG_CATEGORY_EXTERN(LogRewindDebuggerRuntime, Log, All);


namespace RewindDebugger
{

	class REWINDDEBUGGERRUNTIME_API FRewindDebuggerRuntime
	{
	public:
		static void Initialize();
		static void Shutdown();
		static FRewindDebuggerRuntime* Instance() { return InternalInstance; }
			
		void StartRecording();
		void StopRecording();

		void StartRecordingWithArgs(const TArray<FString>& Args);

		bool IsRecording() const { return bIsRecording; }

		FSimpleMulticastDelegate RecordingStarted;
		FSimpleMulticastDelegate ClearRecording;
		FSimpleMulticastDelegate RecordingStopped;
	private:
		void StartRecording(FTraceAuxiliary::EConnectionType TraceType, const TCHAR* TraceDestination);

		bool bIsRecording = false;
		static FRewindDebuggerRuntime* InternalInstance;
	};
}
