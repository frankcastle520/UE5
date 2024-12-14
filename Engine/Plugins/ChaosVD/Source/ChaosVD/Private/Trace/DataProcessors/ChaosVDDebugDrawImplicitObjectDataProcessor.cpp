// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVDDebugDrawImplicitObjectDataProcessor.h"
#include "DataWrappers/ChaosVDDebugShapeDataWrapper.h"

FChaosVDDebugDrawImplicitObjectDataProcessor::FChaosVDDebugDrawImplicitObjectDataProcessor() : FChaosVDDataProcessorBase(FChaosVDDebugDrawImplicitObjectDataWrapper::WrapperTypeName)
{
}

bool FChaosVDDebugDrawImplicitObjectDataProcessor::ProcessRawData(const TArray<uint8>& InData)
{
	FChaosVDDataProcessorBase::ProcessRawData(InData);

	const TSharedPtr<FChaosVDTraceProvider> ProviderSharedPtr = TraceProvider.Pin();
	if (!ensure(ProviderSharedPtr.IsValid()))
	{
		return false;
	}

	const TSharedPtr<FChaosVDDebugDrawImplicitObjectDataWrapper> DebugDrawData = MakeShared<FChaosVDDebugDrawImplicitObjectDataWrapper>();
	const bool bSuccess = Chaos::VisualDebugger::ReadDataFromBuffer(InData, *DebugDrawData, ProviderSharedPtr.ToSharedRef());
	
	if (bSuccess)
	{
		if (TSharedPtr<FChaosVDGameFrameData> CurrentFrameData = ProviderSharedPtr->GetCurrentGameFrame().Pin())
		{
			CurrentFrameData->RecordedDebugDrawImplicitObjectsBySolverID.FindOrAdd(DebugDrawData->SolverID).Add(DebugDrawData);
			CurrentFrameData->MarkDirty();
		}
	}

	return bSuccess;
}
