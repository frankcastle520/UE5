// Copyright Epic Games, Inc. All Rights Reserved.

#include "Trace/DataProcessors/ChaosVDTraceParticleDataProcessor.h"

#include "ChaosVDModule.h"
#include "ChaosVDRecording.h"
#include "ChaosVisualDebugger/ChaosVDMemWriterReader.h"
#include "ChaosVisualDebugger/ChaosVisualDebuggerTrace.h"
#include "DataWrappers/ChaosVDParticleDataWrapper.h"
#include "Trace/ChaosVDTraceProvider.h"

FChaosVDTraceParticleDataProcessor::FChaosVDTraceParticleDataProcessor(): FChaosVDDataProcessorBase(FChaosVDParticleDataWrapper::WrapperTypeName)
{
}

bool FChaosVDTraceParticleDataProcessor::ProcessRawData(const TArray<uint8>& InData)
{
	FChaosVDDataProcessorBase::ProcessRawData(InData);

	TSharedPtr<FChaosVDTraceProvider> ProviderSharedPtr = TraceProvider.Pin();
	if (!ensure(ProviderSharedPtr.IsValid()))
	{
		return false;
	}

	TSharedPtr<FChaosVDParticleDataWrapper> ParticleData = MakeShared<FChaosVDParticleDataWrapper>();
	const bool bSuccess = Chaos::VisualDebugger::ReadDataFromBuffer(InData, *ParticleData, ProviderSharedPtr.ToSharedRef());

	if (bSuccess)
	{
		EChaosVDSolverStageAccessorFlags StageAccessorFlags = EChaosVDSolverStageAccessorFlags::CreateNewIfEmpty | EChaosVDSolverStageAccessorFlags::CreateNewIfClosed; 
		if (FChaosVDStepData* CurrentSolverStage = ProviderSharedPtr->GetCurrentSolverStageDataForCurrentFrame(ParticleData->SolverID, StageAccessorFlags))
		{
			CurrentSolverStage->RecordedParticlesData.Add(ParticleData);
		}
	}

	return bSuccess;
}
