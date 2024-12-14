// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVDDataProcessorBase.h"

FChaosVDDataProcessorBase::~FChaosVDDataProcessorBase()
{
}

FStringView FChaosVDDataProcessorBase::GetCompatibleTypeName() const
{
	return CompatibleType;
}

bool FChaosVDDataProcessorBase::ProcessRawData(const TArray<uint8>& InData)
{
	ProcessedBytes += InData.Num();
	return true;
}

uint64 FChaosVDDataProcessorBase::GetProcessedBytes() const
{
	return ProcessedBytes;
}

void FChaosVDDataProcessorBase::SetTraceProvider(const TSharedPtr<FChaosVDTraceProvider>& InProvider)
{
	TraceProvider = InProvider;
}
