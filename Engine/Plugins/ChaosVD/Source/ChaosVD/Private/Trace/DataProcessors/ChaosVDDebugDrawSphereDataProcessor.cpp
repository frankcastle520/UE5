// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVDDebugDrawSphereDataProcessor.h"
#include "DataWrappers/ChaosVDDebugShapeDataWrapper.h"

FChaosVDDebugDrawSphereDataProcessor::FChaosVDDebugDrawSphereDataProcessor() : FChaosVDDataProcessorBase(FChaosVDDebugDrawSphereDataWrapper::WrapperTypeName)
{
}

bool FChaosVDDebugDrawSphereDataProcessor::ProcessRawData(const TArray<uint8>& InData)
{
	FChaosVDDataProcessorBase::ProcessRawData(InData);

	const TSharedPtr<FChaosVDTraceProvider> ProviderSharedPtr = TraceProvider.Pin();
	if (!ensure(ProviderSharedPtr.IsValid()))
	{
		return false;
	}

	const TSharedPtr<FChaosVDDebugDrawSphereDataWrapper> DebugDrawData = MakeShared<FChaosVDDebugDrawSphereDataWrapper>();
	const bool bSuccess = Chaos::VisualDebugger::ReadDataFromBuffer(InData, *DebugDrawData, ProviderSharedPtr.ToSharedRef());
	
	if (bSuccess)
	{
		if (TSharedPtr<FChaosVDGameFrameData> CurrentFrameData = ProviderSharedPtr->GetCurrentGameFrame().Pin())
		{
			CurrentFrameData->RecordedDebugDrawSpheresBySolverID.FindOrAdd(DebugDrawData->SolverID).Add(DebugDrawData);
			CurrentFrameData->MarkDirty();
		}
	}

	return bSuccess;
}
