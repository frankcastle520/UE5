// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "HAL/Platform.h"

class UMovieGraphEvaluatedConfig;
class UMoviePipelineExecutorJob;
class UMoviePipelineExecutorShot;

/** Telemetry data that is captured when a shot begins rendering. Only for use with settings/nodes shipped with Movie Render Queue/Graph. */
struct MOVIERENDERPIPELINECORE_API FMoviePipelineShotRenderTelemetry
{
	bool bIsGraph = false;
	bool bUsesDeferred = false;
	bool bUsesPathTracer = false;
	bool bUsesPanoramic = false;
	bool bUsesHighResTiling = false;
	bool bUsesNDisplay = false;
	bool bUsesObjectID = false;
	bool bUsesMultiCamera = false;
	bool bUsesScripting = false;
	bool bUsesSubgraphs = false;
	bool bUsesPPMs = false;
	bool bUsesAudio = false;
	bool bUsesAvid = false;
	bool bUsesProRes = false;
	int32 ResolutionX = 0;
	int32 ResolutionY = 0;
	int32 HandleFrameCount = 0;
	int32 TemporalSampleCount = 0;
	int32 SpatialSampleCount = 0;
	int32 RenderLayerCount = 0;

	// Note: If adding an entry here, make sure to also update FMoviePipelineTelemetry::SendBeginShotRenderTelemetry()
	// Also remember to track the telemetry in both the graph and legacy.
};

/** Responsible for sending out telemetry for both Movie Render Queue and Movie Render Graph. */
class MOVIERENDERPIPELINECORE_API FMoviePipelineTelemetry
{
public:
	/** Sends out telemetry that captures queue-level information. */
	static void SendRendersRequestedTelemetry(const bool bIsLocal, TArray<UMoviePipelineExecutorJob*>&& InJobs);

	/** Sends out telemetry that includes information about the shot being rendered (the type of rendering being done, which types of settings/nodes are being used, etc). */
	static void SendBeginShotRenderTelemetry(UMoviePipelineExecutorShot* InShot, UMovieGraphEvaluatedConfig* InEvaluatedConfig = nullptr);

	/** Sends out telemetry that indicates whether the shot was rendered successfully. */
	static void SendEndShotRenderTelemetry(const bool bIsGraph, const bool bWasSuccessful, const bool bWasCanceled);

private:
	/** Gets the current session type (Editor, DashGame, Shipping). */
	static FString GetSessionType();

	/** Returns a populated telemetry object for the shot and graph that's specified. */
	static FMoviePipelineShotRenderTelemetry GatherShotRenderTelemetryForGraph(const UMoviePipelineExecutorShot* InShot, UMovieGraphEvaluatedConfig* InEvaluatedConfig);

	/** Returns a populated telemetry object for the shot that's specified. For use with the legacy system, not the graph. */
	static FMoviePipelineShotRenderTelemetry GatherShotRenderTelemetryForLegacy(UMoviePipelineExecutorShot* InShot);
};