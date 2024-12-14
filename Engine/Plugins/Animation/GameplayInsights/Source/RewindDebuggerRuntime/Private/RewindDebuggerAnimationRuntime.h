// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RewindDebuggerRuntimeInterface/IRewindDebuggerRuntimeExtension.h"


namespace RewindDebugger
{

	class REWINDDEBUGGERRUNTIME_API FRewindDebuggerAnimationRuntime : public IRewindDebuggerRuntimeExtension 
	{
	public:
		virtual void RecordingStarted() override
		{
			UE::Trace::ToggleChannel(TEXT("Animation"), true);
		}
		
		virtual void RecordingStopped() override
		{
			UE::Trace::ToggleChannel(TEXT("Animation"), false);
		}
	};
}
