// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/MovieGraphLinearTimeStep.h"
#include "MovieRenderPipelineCoreModule.h"

#include "Graph/Nodes/MovieGraphSamplingMethodNode.h"

int32 UMovieGraphLinearTimeStep::GetNextTemporalRangeIndex() const
{
	// Linear time step just steps through the temporal ranges in order
	return CurrentFrameData.TemporalSampleIndex;
}

int32 UMovieGraphLinearTimeStep::GetTemporalSampleCount() const
{
	return GetTemporalSampleCountFromConfig(CurrentFrameData.EvaluatedConfig.Get());
}
