// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVDDebugDrawLineDataProcessor.h"
#include "DataWrappers/ChaosVDDebugShapeDataWrapper.h"

FChaosVDDebugDrawLineDataProcessor::FChaosVDDebugDrawLineDataProcessor() : FChaosVDDataProcessorBase(FChaosVDDebugDrawLineDataWrapper::WrapperTypeName)
{
}

bool FChaosVDDebugDrawLineDataProcessor::ProcessRawData(const TArray<uint8>& InData)
{
	FChaosVDDataProcessorBase::ProcessRawData(InData);

	const TSharedPtr<FChaosVDTraceProvider> ProviderSharedPtr = TraceProvider.Pin();
	if (!ensure(ProviderSharedPtr.IsValid()))
	{
		return false;
	}

	const TSharedPtr<FChaosVDDebugDrawLineDataWrapper> DebugDrawData = MakeShared<FChaosVDDebugDrawLineDataWrapper>();
	const bool bSuccess = Chaos::VisualDebugger::ReadDataFromBuffer(InData, *DebugDrawData, ProviderSharedPtr.ToSharedRef());
	
	if (bSuccess)
	{
		if (TSharedPtr<FChaosVDGameFrameData> CurrentFrameData = ProviderSharedPtr->GetCurrentGameFrame().Pin())
		{
			CurrentFrameData->RecordedDebugDrawLinesBySolverID.FindOrAdd(DebugDrawData->SolverID).Add(DebugDrawData);
			CurrentFrameData->MarkDirty();
		}
	}

	return bSuccess;
}