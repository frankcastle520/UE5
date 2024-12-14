// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVDCollisionChannelsInfoDataProcessor.h"

FChaosVDCollisionChannelsInfoDataProcessor::FChaosVDCollisionChannelsInfoDataProcessor() : FChaosVDDataProcessorBase(FChaosVDCollisionChannelsInfoContainer::WrapperTypeName)
{
	
}

bool FChaosVDCollisionChannelsInfoDataProcessor::ProcessRawData(const TArray<uint8>& InData)
{
	FChaosVDDataProcessorBase::ProcessRawData(InData);

	TSharedPtr<FChaosVDTraceProvider> ProviderSharedPtr = TraceProvider.Pin();
	if (!ensure(ProviderSharedPtr.IsValid()))
	{
		return false;
	}

	TSharedPtr<FChaosVDCollisionChannelsInfoContainer> CollisionChannelsData = MakeShared<FChaosVDCollisionChannelsInfoContainer>();
	const bool bSuccess = Chaos::VisualDebugger::ReadDataFromBuffer(InData, *CollisionChannelsData, ProviderSharedPtr.ToSharedRef());
	
	if (bSuccess)
	{
		ProviderSharedPtr->GetRecordingForSession()->SetCollisionChannelsInfoContainer(CollisionChannelsData);
	}
	
	return bSuccess;
}
