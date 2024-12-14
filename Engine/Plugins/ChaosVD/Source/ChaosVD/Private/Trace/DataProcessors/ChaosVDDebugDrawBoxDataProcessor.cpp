// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVDDebugDrawBoxDataProcessor.h"
#include "DataWrappers/ChaosVDDebugShapeDataWrapper.h"

FChaosVDDebugDrawBoxDataProcessor::FChaosVDDebugDrawBoxDataProcessor() : FChaosVDDataProcessorBase(FChaosVDDebugDrawBoxDataWrapper::WrapperTypeName)
{
}

bool FChaosVDDebugDrawBoxDataProcessor::ProcessRawData(const TArray<uint8>& InData)
{
	FChaosVDDataProcessorBase::ProcessRawData(InData);

	const TSharedPtr<FChaosVDTraceProvider> ProviderSharedPtr = TraceProvider.Pin();
	if (!ensure(ProviderSharedPtr.IsValid()))
	{
		return false;
	}

	const TSharedPtr<FChaosVDDebugDrawBoxDataWrapper> DebugDrawData = MakeShared<FChaosVDDebugDrawBoxDataWrapper>();
	const bool bSuccess = Chaos::VisualDebugger::ReadDataFromBuffer(InData, *DebugDrawData, ProviderSharedPtr.ToSharedRef());
	
	if (bSuccess)
	{
		if (TSharedPtr<FChaosVDGameFrameData> CurrentFrameData = ProviderSharedPtr->GetCurrentGameFrame().Pin())
		{
			CurrentFrameData->RecordedDebugDrawBoxesBySolverID.FindOrAdd(DebugDrawData->SolverID).Add(DebugDrawData);
			CurrentFrameData->MarkDirty();
		}
	}

	return bSuccess;
}
