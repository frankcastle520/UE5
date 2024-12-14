// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Async/AsyncWork.h"
#include "Async/TaskGraphInterfaces.h"
#include "ImagePixelData.h"
#include "MovieGraphFileOutputNode.h"
#include "Stats/Stats.h"
#include "Templates/Function.h"

#include "MovieGraphVideoOutputNode.generated.h"

namespace MovieRenderGraph
{
	struct IVideoCodecWriter
	{
		/**
		 * The filename without de-duplication numbers, used to match up multiple incoming frames back to the same writer.
		 * We use this when looking for existing writers so that we can avoid the de-duplication numbers perpetually increasing
		 * due to the file existing on disk after the first frame comes in, and then the next one de-duplicating to one more than that.
		 */
		FString StableFileName;
	};
}

/**
 * A base node for nodes that generate video in the Movie Render Graph.
 */
UCLASS(BlueprintType, Abstract)
class MOVIERENDERPIPELINECORE_API UMovieGraphVideoOutputNode : public UMovieGraphFileOutputNode
{
	GENERATED_BODY()

public:
	UMovieGraphVideoOutputNode();

protected:
	// The parameters to Initialize_GameThread() have changed a lot -- using a struct as a the only parameter will make future changes easier.
	struct FMovieGraphVideoNodeInitializationContext
	{
		UMovieGraphPipeline* Pipeline;
		TObjectPtr<UMovieGraphEvaluatedConfig> EvaluatedConfig;
		const FMovieGraphTraversalContext* TraversalContext;
		const FMovieGraphPassData* PassData;
		FString FileName;
		bool bAllowOCIO;
	};
	
	// UMovieGraphFileOutputNode Interface
	virtual void OnReceiveImageDataImpl(UMovieGraphPipeline* InPipeline, UE::MovieGraph::FMovieGraphOutputMergerFrame* InRawFrameData, const TSet<FMovieGraphRenderDataIdentifier>& InMask) override;
	virtual void OnAllFramesSubmittedImpl(UMovieGraphPipeline* InPipeline, TObjectPtr<UMovieGraphEvaluatedConfig>& InPrimaryJobEvaluatedGraph) override;
	virtual void OnAllFramesFinalizedImpl(UMovieGraphPipeline* InPipeline, TObjectPtr<UMovieGraphEvaluatedConfig>& InPrimaryJobEvaluatedGraph) override;
	virtual void OnAllShotFramesSubmittedImpl(UMovieGraphPipeline* InPipeline, const UMoviePipelineExecutorShot* InShot, TObjectPtr<UMovieGraphEvaluatedConfig>& InShotEvaluatedGraph) override;
	virtual bool IsFinishedWritingToDiskImpl() const override;
	// ~UMovieGraphFileOutputNode Interface

	virtual TUniquePtr<MovieRenderGraph::IVideoCodecWriter> Initialize_GameThread(const FMovieGraphVideoNodeInitializationContext& InInitializationContext)  PURE_VIRTUAL(UMovieGraphVideoOutputNode::Initialize_GameThread, return nullptr; );
	virtual bool Initialize_EncodeThread(MovieRenderGraph::IVideoCodecWriter* InWriter) PURE_VIRTUAL(UMovieGraphVideoOutputNode::Initialize_EncodeThread, return true;);
	virtual void WriteFrame_EncodeThread(MovieRenderGraph::IVideoCodecWriter* InWriter, FImagePixelData* InPixelData, TArray<FMovieGraphPassData>&& InCompositePasses, TObjectPtr<UMovieGraphEvaluatedConfig> InEvaluatedConfig, const FString& InBranchName) PURE_VIRTUAL(UMovieGraphVideoOutputNode::WriteFrame_EncodeThread);
	virtual void BeginFinalize_EncodeThread(MovieRenderGraph::IVideoCodecWriter* InWriter) PURE_VIRTUAL(UMovieGraphVideoOutputNode::BeginFinalize_EncodeThread);
	virtual void Finalize_EncodeThread(MovieRenderGraph::IVideoCodecWriter* InWriter) PURE_VIRTUAL(UMovieGraphVideoOutputNode::Finalize_EncodeThread);
	virtual const TCHAR* GetFilenameExtension() const PURE_VIRTUAL(UMovieGraphVideoOutputNode::GetFilenameExtension, return TEXT(""););
	virtual bool IsAudioSupported() const PURE_VIRTUAL(UMovieGraphVideoOutputNode::IsAudioSupported, return false;);

private:
	struct FMovieGraphCodecWriterWithPromise
	{
		FMovieGraphCodecWriterWithPromise(TUniquePtr<MovieRenderGraph::IVideoCodecWriter>&& InWriter, TPromise<bool>&& InPromise, UClass* InNodeType);
		
		/** The codec writer. */
		TUniquePtr<MovieRenderGraph::IVideoCodecWriter> CodecWriter;

		/** The promise that is provided to the pipeline that specifies whether or not the writer has finished. */
		TPromise<bool> Promise;
		
		/** The type of node associated with this writer. */
		UClass* NodeType;
	};

	/**
	 * Generates a "stable" and "final" filename for a writer. The stable filename has not been put through a de-duplication procedure (ie, it
	 * might reference an existing file on disk). The final filename is what will be written to disk and will not reference an existing filename
	 * on disk (unless the user has specified that overwriting existing files is ok).
	 */
	void GetOutputFilePaths(const UMovieGraphPipeline* InPipeline, const UE::MovieGraph::FMovieGraphOutputMergerFrame* InRawFrameData, FMovieGraphPassData& InRenderPassData, const TArray<FMovieGraphPassData>& InCompositedPasses, FString& OutFinalFilePath, FString& OutStableFilePath);

	/**
	 * Generates the writer that is responsible for doing the encoding work. The writer will be added to AllWriters. If there was a problem creating
	 * the writer, nullptr will be returned and an error will be sent to the log.
	 */
	FMovieGraphCodecWriterWithPromise* GetOrCreateOutputWriter(UMovieGraphPipeline* InPipeline, const UE::MovieGraph::FMovieGraphOutputMergerFrame* InRawFrameData, FMovieGraphPassData& InRenderPassData, const TArray<FMovieGraphPassData>& InCompositedPasses);

private:
	// The pipeline generates many instances of the same node throughout its execution; however, some nodes need to have persistent data throughout the
	// pipeline's lifetime. This static data enables the node to have shared data across instances.
	/** All writers that are currently being run. There is one writer per filename. There might be multiple writers due to multiple passes being written out. */
	inline static TArray<FMovieGraphCodecWriterWithPromise> AllWriters;

	/** Whether the output encountered any error, like failing to initialize properly. */
	bool bHasError;
};