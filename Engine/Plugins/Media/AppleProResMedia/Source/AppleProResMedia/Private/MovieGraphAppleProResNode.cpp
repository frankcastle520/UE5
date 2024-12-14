// Copyright Epic Games, Inc. All Rights Reserved.

#include "MovieGraphAppleProResNode.h"

#include "AppleProResEncoder/AppleProResEncoder.h"
#include "Graph/MovieGraphBlueprintLibrary.h"
#include "Graph/MovieGraphConfig.h"
#include "Graph/MovieGraphPipeline.h"
#include "Graph/MovieGraphOCIOHelper.h"
#include "Graph/Nodes/MovieGraphGlobalOutputSettingNode.h"
#include "ImageWriteTask.h"
#include "MoviePipelineImageQuantization.h"
#include "MoviePipelineTelemetry.h"
#include "MovieRenderPipelineCoreModule.h"
#include "Styling/AppStyle.h"

UMovieGraphAppleProResNode::UMovieGraphAppleProResNode()
	: Quality(EAppleProResEncoderCodec::ProRes_422LT)
{
	
}

#if WITH_EDITOR
FText UMovieGraphAppleProResNode::GetNodeTitle(const bool bGetDescriptive) const
{
	static const FText AppleProResNodeName = NSLOCTEXT("MovieGraphNodes", "NodeName_AppleProRes", "Apple ProRes Movie");
	return AppleProResNodeName;
}

FText UMovieGraphAppleProResNode::GetMenuCategory() const
{
	return NSLOCTEXT("MovieGraphNodes", "AppleProResNode_Category", "Output Type");
}

FText UMovieGraphAppleProResNode::GetKeywords() const
{
	static const FText Keywords = NSLOCTEXT("MovieGraphNodes", "AppleProResGraphNode_Keywords", "apple pro res prores mov movie video");
	return Keywords;
}

FLinearColor UMovieGraphAppleProResNode::GetNodeTitleColor() const
{
	static const FLinearColor AppleProResNodeColor = FLinearColor(0.047f, 0.654f, 0.537f);
	return AppleProResNodeColor;
}

FSlateIcon UMovieGraphAppleProResNode::GetIconAndTint(FLinearColor& OutColor) const
{
	static const FSlateIcon AppleProResIcon = FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Cinematics");

	OutColor = FLinearColor::White;
	return AppleProResIcon;
}
#endif // WITH_EDITOR

TUniquePtr<MovieRenderGraph::IVideoCodecWriter> UMovieGraphAppleProResNode::Initialize_GameThread(const FMovieGraphVideoNodeInitializationContext& InInitializationContext)
{
	bool bIncludeCDOs = true;
	constexpr bool bExactMatch = true;
	UMovieGraphGlobalOutputSettingNode* OutputSetting =
		InInitializationContext.EvaluatedConfig->GetSettingForBranch<UMovieGraphGlobalOutputSettingNode>(GlobalsPinName, bIncludeCDOs, bExactMatch);

	bIncludeCDOs = false;
	const UMovieGraphAppleProResNode* EvaluatedNode = Cast<UMovieGraphAppleProResNode>(
		InInitializationContext.EvaluatedConfig->GetSettingForBranch(GetClass(), FName(InInitializationContext.PassData->Key.RootBranchName), bIncludeCDOs, bExactMatch));
	checkf(EvaluatedNode, TEXT("Apple ProRes node could not be found in the graph in branch [%s]."), *InInitializationContext.PassData->Key.RootBranchName.ToString());
	
	const FFrameRate SourceFrameRate = InInitializationContext.Pipeline->GetDataSourceInstance()->GetDisplayRate();
	const FFrameRate EffectiveFrameRate = UMovieGraphBlueprintLibrary::GetEffectiveFrameRate(OutputSetting, SourceFrameRate);
	
	FAppleProResEncoderOptions Options;
	Options.OutputFilename = InInitializationContext.FileName;
	Options.Width = InInitializationContext.PassData->Value->GetSize().X;
	Options.Height = InInitializationContext.PassData->Value->GetSize().Y;
	Options.FrameRate = EffectiveFrameRate;
	Options.Codec = EvaluatedNode->Quality;
	Options.ColorPrimaries = EAppleProResEncoderColorPrimaries::CD_HDREC709; // Force Rec 709 for now
	Options.ScanMode = EAppleProResEncoderScanMode::IM_PROGRESSIVE_SCAN; // No interlace sources
	Options.bWriteAlpha = true;
	Options.bDropFrameTimecode = OutputSetting->bDropFrameTimecode;

	// If OCIO is enabled, don't do additional color conversion
	Options.bConvertToSrgb = !(EvaluatedNode->bOverride_OCIOConfiguration && EvaluatedNode->OCIOConfiguration.bIsEnabled && InInitializationContext.bAllowOCIO);
	
	TUniquePtr<FProResWriter> NewWriter = MakeUnique<FProResWriter>();
	NewWriter->Writer = MakeUnique<FAppleProResEncoder>(Options);

	CachedPipeline = InInitializationContext.Pipeline;
	
	return NewWriter;
}

bool UMovieGraphAppleProResNode::Initialize_EncodeThread(MovieRenderGraph::IVideoCodecWriter* InWriter)
{
	const FProResWriter* CodecWriter = static_cast<FProResWriter*>(InWriter);
	if(!CodecWriter->Writer->Initialize())
	{
		UE_LOG(LogMovieRenderPipeline, Error, TEXT("Failed to initialize Apple ProRes writer."));
		return false;
	}
	
	return true;
}

void UMovieGraphAppleProResNode::WriteFrame_EncodeThread(MovieRenderGraph::IVideoCodecWriter* InWriter, FImagePixelData* InPixelData, TArray<FMovieGraphPassData>&& InCompositePasses, TObjectPtr<UMovieGraphEvaluatedConfig> InEvaluatedConfig, const FString& InBranchName)
{
	bool bIncludeCDOs = false;
	constexpr bool bExactMatch = true;
	const UMovieGraphAppleProResNode* EvaluatedNode = Cast<UMovieGraphAppleProResNode>(
		InEvaluatedConfig->GetSettingForBranch(GetClass(), FName(InBranchName), bIncludeCDOs, bExactMatch));
	checkf(EvaluatedNode, TEXT("Apple ProRes node could not be found in the graph in branch [%s]."), *InBranchName);

	bIncludeCDOs = true;
	const UMovieGraphGlobalOutputSettingNode* OutputSettingNode =
		InEvaluatedConfig->GetSettingForBranch<UMovieGraphGlobalOutputSettingNode>(GlobalsPinName, bIncludeCDOs, bExactMatch);
	
	const FProResWriter* CodecWriter = static_cast<FProResWriter*>(InWriter);
	
	const UE::MovieGraph::FMovieGraphSampleState* GraphPayload = InPixelData->GetPayload<UE::MovieGraph::FMovieGraphSampleState>();
	
	// Translate our Movie Pipeline specific payload to a ProRes Encoder specific payload.
	const TSharedPtr<FAppleProResEncoder::FTimecodePayload, ESPMode::ThreadSafe> ProResPayload = MakeShared<FAppleProResEncoder::FTimecodePayload, ESPMode::ThreadSafe>();

	if (OutputSettingNode->bOverride_CustomTimecodeStart)
	{
		const int32 CustomTimecodeStartFrame = OutputSettingNode->CustomTimecodeStart.ToFrameNumber(CodecWriter->Writer->GetOptions().FrameRate).Value;
		const FMovieGraphTimeStepData& TimeData = GraphPayload->TraversalContext.Time;
		
		// When using a custom timecode start, just use the root-level frame number (relative to zero) offset by the custom timecode start
		ProResPayload->ReferenceFrameNumber = TimeData.OutputFrameNumber + CustomTimecodeStartFrame;
	}
	else
	{
		// This is the frame number on the global time, can have overlaps (between encoders) or repeats when using handle frames/slowmo.
		ProResPayload->ReferenceFrameNumber = GraphPayload->TraversalContext.Time.RootFrameNumber.Value;
	}

	// ProRes can handle quantization internally but expects sRGB to be applied to the incoming data.
	const TUniquePtr<FImagePixelData> QuantizedPixelData = UE::MoviePipeline::QuantizeImagePixelDataToBitDepth(InPixelData, 8, ProResPayload, CodecWriter->Writer->GetOptions().bConvertToSrgb);

	TArray<FPixelPreProcessor> PixelPreProcessors;

#if WITH_OCIO
	FMovieGraphOCIOHelper::GenerateOcioPixelPreProcessor(GraphPayload, CachedPipeline.Get(), InEvaluatedConfig, OCIOConfiguration, OCIOContext, PixelPreProcessors);
#endif

	// Do a quick composite of renders/burn-ins.
	for (const FMovieGraphPassData& CompositePass : InCompositePasses)
	{
		// We don't need to copy the data here (even though it's being passed to a async system) because we already made a unique copy of the
		// burn in/widget data when we decided to composite it.
		switch (QuantizedPixelData->GetType())
		{
		case EImagePixelType::Color:
			PixelPreProcessors.Add(TAsyncCompositeImage<FColor>(CompositePass.Value->MoveImageDataToNew()));
			break;
		case EImagePixelType::Float16:
			PixelPreProcessors.Add(TAsyncCompositeImage<FFloat16Color>(CompositePass.Value->MoveImageDataToNew()));
			break;
		case EImagePixelType::Float32:
			PixelPreProcessors.Add(TAsyncCompositeImage<FLinearColor>(CompositePass.Value->MoveImageDataToNew()));
			break;
		}
	}

	// This is done on the current thread for simplicity but the composite itself is parallelized.
	FImagePixelData* PixelData = QuantizedPixelData.Get();
	for (const FPixelPreProcessor& PreProcessor : PixelPreProcessors)
	{
		// PreProcessors are assumed to be valid.
		PreProcessor(PixelData);
	}

	CodecWriter->Writer->WriteFrame(PixelData);
}

void UMovieGraphAppleProResNode::BeginFinalize_EncodeThread(MovieRenderGraph::IVideoCodecWriter* InWriter)
{
	return;
}

void UMovieGraphAppleProResNode::Finalize_EncodeThread(MovieRenderGraph::IVideoCodecWriter* InWriter)
{
	// Write to disk
	const FProResWriter* CodecWriter = static_cast<FProResWriter*>(InWriter);
	CodecWriter->Writer->Finalize();
}

const TCHAR* UMovieGraphAppleProResNode::GetFilenameExtension() const
{
	return TEXT("mov");
}

bool UMovieGraphAppleProResNode::IsAudioSupported() const
{
	// The current ProRes SDK does not support audio so we don't write audio.
	return false;
}

void UMovieGraphAppleProResNode::UpdateTelemetry(FMoviePipelineShotRenderTelemetry* InTelemetry) const
{
	InTelemetry->bUsesProRes = true;
}
