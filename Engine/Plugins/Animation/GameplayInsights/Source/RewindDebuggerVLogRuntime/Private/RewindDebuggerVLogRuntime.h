// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RewindDebuggerRuntimeInterface/IRewindDebuggerRuntimeExtension.h"
#include "VisualLogger/VisualLogger.h"

namespace RewindDebugger
{

	class REWINDDEBUGGERVLOGRUNTIME_API FRewindDebuggerVLogRuntime : public IRewindDebuggerRuntimeExtension 
	{
	public:
		virtual void RecordingStarted() override
		{
#if ENABLE_VISUAL_LOG	
			// start recording visual logger data
			FVisualLogger::Get().SetIsRecordingToTrace(true);
#endif
		}
		
		virtual void RecordingStopped() override
		{
#if ENABLE_VISUAL_LOG	
			// stop recording visual logger data
			FVisualLogger::Get().SetIsRecordingToTrace(false);
#endif
		}
	};
}
