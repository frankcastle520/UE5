// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Graph/Nodes/MovieGraphFileOutputNode.h"
#include "Sound/SampleBufferIO.h"

#include "MovieGraphAudioOutputNode.generated.h"

/**
 * A node which outputs .wav audio after all renders have completed.
 */
UCLASS()
class UMovieGraphAudioOutputNode : public UMovieGraphFileOutputNode
{
	GENERATED_BODY()
	
public:
	UMovieGraphAudioOutputNode();

	// UMovieGraphSettingNode Interface
	virtual void BuildNewProcessCommandLineArgsImpl(TArray<FString>& InOutUnrealURLParams, TArray<FString>& InOutCommandLineArgs, TArray<FString>& InOutDeviceProfileCvars, TArray<FString>& InOutExecCmds) const override;
	virtual void UpdateTelemetry(FMoviePipelineShotRenderTelemetry* InTelemetry) const override;
	// ~UMovieGraphSettingNode Interface

	// UMovieGraphNode Interface
	virtual EMovieGraphBranchRestriction GetBranchRestriction() const override;

#if WITH_EDITOR
	virtual FText GetNodeTitle(const bool bGetDescriptive) const override;
	virtual FText GetKeywords() const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;
#endif
	// ~UMovieGraphNode Interface

	/**
	 * Although this node does not represent a proper "renderer", it will sometimes be identified in data structures
	 * as a renderer. If that's the case, this is the name of the audio "renderer".
	 */
	inline static const FString RendererName = TEXT("Audio");

	/** The extension of the audio files generated by this node. */
	inline static const FString OutputExtension = TEXT("wav");


protected:
	// UMovieGraphFileOutputNode Interface
	virtual void OnAllFramesSubmittedImpl(UMovieGraphPipeline* InPipeline, TObjectPtr<UMovieGraphEvaluatedConfig>& InPrimaryJobEvaluatedGraph) override;
	virtual void OnAllShotFramesSubmittedImpl(UMovieGraphPipeline* InPipeline, const UMoviePipelineExecutorShot* InShot, TObjectPtr<UMovieGraphEvaluatedConfig>& InShotEvaluatedGraph) override;
	virtual bool IsFinishedWritingToDiskImpl() const override;
	// ~UMovieGraphFileOutputNode Interface

private:
	/** Represents an audio file, with its associated audio data. */
	struct FFinalAudioData
	{
		FString FilePath;
		Audio::TSampleBuffer<int16> SampleBuffer;
		int32 ShotIndex;
		FMovieGraphRenderDataIdentifier RenderIdentifier;
	};

	/** Generate the final output path for the audio file. */
	FString GenerateOutputPath(const FMovieGraphRenderDataIdentifier& InRenderIdentifier, const TObjectPtr<UMoviePipelineExecutorShot>& InShot) const;

	/** Generates final audio data from all of the audio segments that were collected during render time. */
	void GenerateFinalAudioData(TArray<FFinalAudioData>& OutFinalAudioData);

	/** Begins the audio export process. */
	void StartAudioExport();
	
	/** Returns true if there should be one export per shot, else false. */
	bool NeedsPerShotFlushing() const;

// The pipeline generates many instances of the same node throughout its execution; however, some nodes need to have persistent data throughout the
// pipeline's lifetime. This static data enables the node to have shared data across instances.
private:
	/** Kept alive during finalization because the writer writes async to disk but doesn't expect to fall out of scope. */
	inline static TArray<TUniquePtr<Audio::FSoundWavePCMWriter>> ActiveWriters;

	/** Keep track of segments that have already been written to disk to avoid re-writing them (and generating new output futures). */
	inline static TSet<FGuid> AlreadyWrittenSegments;

private:
	/** The pipeline that is running this node. */
	TWeakObjectPtr<UMovieGraphPipeline> CachedPipeline;

	/**
	 * The graph that should be accessed during execution of the node. Do not access the graph from the pipeline as it may be invalid depending on
	 * if a shot-level or sequence-level export is being performed.
	 */
	TObjectPtr<UMovieGraphEvaluatedConfig> EvaluatedGraph;
};