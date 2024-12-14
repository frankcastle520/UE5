// Copyright Epic Games, Inc. All Rights Reserved.

#include "Trace/DataProcessors/ChaosVDCharacterGroundConstraintDataProcessor.h"

#include "DataWrappers/ChaosVDCharacterGroundConstraintDataWrappers.h"
#include "Trace/ChaosVDTraceProvider.h"


FChaosVDCharacterGroundConstraintDataProcessor::FChaosVDCharacterGroundConstraintDataProcessor()
	: FChaosVDDataProcessorBase(FChaosVDCharacterGroundConstraint::WrapperTypeName)
{
}

bool FChaosVDCharacterGroundConstraintDataProcessor::ProcessRawData(const TArray<uint8>& InData)
{
	FChaosVDDataProcessorBase::ProcessRawData(InData);

	TSharedPtr<FChaosVDTraceProvider> ProviderSharedPtr = TraceProvider.Pin();
	if (!ensure(ProviderSharedPtr.IsValid()))
	{
		return false;
	}

	TSharedPtr<FChaosVDCharacterGroundConstraint> Constraint = MakeShared<FChaosVDCharacterGroundConstraint>();
	const bool bSuccess = Chaos::VisualDebugger::ReadDataFromBuffer(InData, *Constraint, ProviderSharedPtr.ToSharedRef());

	if (bSuccess)
	{
		if (FChaosVDSolverFrameData* CurrentFrameData = ProviderSharedPtr->GetCurrentSolverFrame(Constraint->SolverID))
		{
			CurrentFrameData->RecordedCharacterGroundConstraints.Add(Constraint);
		}
	}

	return bSuccess;
}
