// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Graph/Nodes/MovieGraphImagePassBaseNode.h"

#include "MovieGraphPathTracerPassNode.generated.h"


UENUM(BlueprintType)
enum class EMovieGraphPathTracerDenoiserType : uint8
{
	/** 
	* The active spatial denoiser plugin will be used for denoising. If the denoiser is not loaded, a warning will show in the log.
	* If multiple spatial denoiser plugins are enabled, the last one to get loaded will be the one used.
	*/
	Spatial = 0,

	/** 
	* The active spatial-temporal denoiser plugin will be used for denoising. It provides more temporal stability than spatial denoiser 
	* if the Frame Count of past/future frames are used (Frame Count > 0) in the plugin. The user needs to config `Frame Count` to
	* match the requirements of the chosen denoiser plugin. If the denoiser is not loaded, a warning will show in the log. If multiple
	* spatial-temporal denoiser plugins are enabled, the last one to get loaded will be the one used.
	*/
	Temporal = 1
};


/** A render node which uses the path tracer. */
UCLASS()
class MOVIERENDERPIPELINERENDERPASSES_API UMovieGraphPathTracerRenderPassNode : public UMovieGraphImagePassBaseNode
{
	GENERATED_BODY()

public:
	UMovieGraphPathTracerRenderPassNode();

#if WITH_EDITOR
	virtual FText GetNodeTitle(const bool bGetDescriptive = false) const override;
	virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;
#endif

	// UMovieGraphRenderPassNode Interface
	virtual void SetupImpl(const FMovieGraphRenderPassSetupData& InSetupData) override;
	virtual void TeardownImpl() override;
	// ~UMovieGraphRenderPassNode Interface

	// UMovieGraphImagePassBaseNode Interface
	virtual bool GetWriteAllSamples() const override;
	virtual TArray<FMoviePipelinePostProcessPass> GetAdditionalPostProcessMaterials() const override;
	virtual int32 GetNumSpatialSamples() const override;
	virtual int32 GetNumSpatialSamplesDuringWarmUp() const override;
	virtual bool GetDisableToneCurve() const override;
	virtual bool GetAllowOCIO() const override;
	virtual bool GetAllowDenoiser() const override;
	virtual TUniquePtr<UE::MovieGraph::Rendering::FMovieGraphImagePassBase> CreateInstance() const;
	virtual FEngineShowFlags GetShowFlags() const override;
	// ~UMovieGraphImagePassBaseNode Interface

	virtual void UpdateTelemetry(FMoviePipelineShotRenderTelemetry* InTelemetry) const override;

protected:
	// UMovieGraphRenderPassNode Interface
	virtual FString GetRendererNameImpl() const override;
	virtual int32 GetCoolingDownFrameCount() const override;
	// ~UMovieGraphRenderPassNode Interface

	// UMovieGraphCoreRenderPassNode Interface
	virtual EViewModeIndex GetViewModeIndex() const override;
	// ~UMovieGraphCoreRenderPassNode Interface

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Overrides, meta = (InlineEditConditionToggle))
	uint8 bOverride_SpatialSampleCount : 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Overrides, meta = (InlineEditConditionToggle))
	uint8 bOverride_bEnableReferenceMotionBlur : 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Overrides, meta = (InlineEditConditionToggle))
	uint8 bOverride_bEnableDenoiser : 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Overrides, meta = (InlineEditConditionToggle))
	uint8 bOverride_DenoiserType : 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Overrides, meta = (InlineEditConditionToggle))
	uint8 bOverride_FrameCount : 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Overrides, meta = (InlineEditConditionToggle))
	uint8 bOverride_bDisableToneCurve : 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Overrides, meta = (InlineEditConditionToggle))
	uint8 bOverride_bAllowOCIO : 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Overrides, meta = (InlineEditConditionToggle))
	uint8 bOverride_bLightingComponents_IncludeEmissive : 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Overrides, meta = (InlineEditConditionToggle))
	uint8 bOverride_bLightingComponents_IncludeDiffuse : 1;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Overrides, meta = (InlineEditConditionToggle))
	uint8 bOverride_bLightingComponents_IncludeIndirectDiffuse : 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Overrides, meta = (InlineEditConditionToggle))
	uint8 bOverride_bLightingComponents_IncludeSpecular : 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Overrides, meta = (InlineEditConditionToggle))
	uint8 bOverride_bLightingComponents_IncludeIndirectSpecular : 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Overrides, meta = (InlineEditConditionToggle))
	uint8 bOverride_bLightingComponents_IncludeVolume : 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Overrides, meta = (InlineEditConditionToggle))
	uint8 bOverride_bLightingComponents_IncludeIndirectVolume : 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Overrides, meta = (InlineEditConditionToggle))
	uint8 bOverride_bWriteAllSamples : 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Overrides, meta = (InlineEditConditionToggle))
	uint8 bOverride_AdditionalPostProcessMaterials : 1;

	/**
	* How many sub-pixel jitter renders should we do per temporal sample? This can be used to achieve high
	* sample counts without Temporal Sub-Sampling (allowing high sample counts without motion blur being enabled),
	* but we generally recommend using Temporal Sub-Samples when possible. It can also be combined with
	* temporal samples and you will get SpatialSampleCount many renders per temporal sample.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (UIMin = 1, ClampMin = 1), Category = "Sampling", meta = (EditCondition = "bOverride_SpatialSampleCount"))
	int32 SpatialSampleCount;

	/** 
	 *  When enabled, the path tracer will blend all spatial and temporal samples prior to the denoising and will disable post-processed motion blur.
	 *  In this mode it is possible to use higher temporal sample counts to improve the motion blur quality. This mode also automatically enabled reference DOF.
	 *  When this option is disabled, the path tracer will accumulate spatial samples, but denoise them prior to accumulation of temporal samples.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (UIMin = 1, ClampMin = 1), Category = "Reference Motion Blur", meta = (EditCondition = "bOverride_bEnableReferenceMotionBlur"))
	bool bEnableReferenceMotionBlur;

	/** If true the resulting image will be denoised at the end of each set of Spatial Samples. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Denoiser", meta = (EditCondition = "bOverride_bEnableDenoiser"))
	bool bEnableDenoiser;

	/**
	* Select which type of denoiser to use when the denoiser is enabled. Temporal denoisers will provide better results when
	* denoising animated sequences (the denoising results will look more stable), especially when combined with an appropriate 
	* Frame Count (non-zero). Denoisers are implemented as plugins so you may need to enable a plugin as well for this to work.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Denoiser", meta = (EditCondition = "bOverride_DenoiserType"))
	EMovieGraphPathTracerDenoiserType DenoiserType;

	/** 
	* The number of frames to consider when using temporal-based denoisers. Generally higher numbers will result in longer
	* denoising times and higher memory requirements. For NFOR this number refers to how many frames to consider on both sides
	* of the current frame (ie: 2 means consider 2 before, and 2 after the currently denoised frame), but other denoiser 
	* implementations may interpret this value differently.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (UIMin = 0, ClampMin = 0, UIMax = 3), Category = "Denoiser")
	int32 FrameCount;

	/**
	* Debug Feature. This can be used to write out each individual spatial sample rendered by this render pass,
	* which allows you to see which images are being accumulated together. Can be useful for debugging incorrect looking
	* frames to see which sub-frame evaluations were incorrect.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sampling", AdvancedDisplay, DisplayName = "Write All Samples (Debug)", meta = (EditCondition = "bOverride_bWriteAllsamples"))
	bool bWriteAllSamples;

	/**
	* If true, the tone curve will be disabled for this render pass. This will result in values greater than 1.0 in final renders
	* and can optionally be combined with OCIO profiles on the file output nodes to convert from Linear Values in Working Color Space
	* (which is sRGB  (Rec. 709) by default, unless changed in the project settings).
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post Processing", meta = (EditCondition = "bOverride_bDisableToneCurve"))
	bool bDisableToneCurve;

	/**
	* Allow the output file OpenColorIO transform to be used on this render.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post Processing", meta = (EditCondition = "bOverride_bAllowOCIO"))
	bool bAllowOCIO;

	/** Whether the render should include directly visible emissive components. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting Components", DisplayName = "Emissive", meta = (EditCondition = "bOverride_bLightingComponents_IncludeEmissive"))
	bool bLightingComponents_IncludeEmissive = true;

	/** Whether the render should include diffuse lighting contributions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting Components", DisplayName = "Diffuse", meta = (EditCondition = "bOverride_bLightingComponents_IncludeDiffuse"))
	bool bLightingComponents_IncludeDiffuse = true;

	/** Whether the render should include indirect diffuse lighting contributions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting Components", DisplayName = "Indirect Diffuse", meta = (EditCondition = "bOverride_bLightingComponents_IncludeIndirectDiffuse"))
	bool bLightingComponents_IncludeIndirectDiffuse = true;

	/** Whether the render should include specular lighting contributions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting Components", DisplayName = "Specular", meta = (EditCondition = "bOverride_bLightingComponents_IncludeSpecular"))
	bool bLightingComponents_IncludeSpecular = true;

	/** Whether the render should include indirect specular lighting contributions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting Components", DisplayName = "Indirect Specular", meta = (EditCondition = "bOverride_bLightingComponents_IncludeIndirectSpecular"))
	bool bLightingComponents_IncludeIndirectSpecular = true;

	/** Whether the render should include volume lighting contributions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting Components", DisplayName = "Volume", meta = (EditCondition = "bOverride_bLightingComponents_IncludeVolume"))
	bool bLightingComponents_IncludeVolume = true;

	/** Whether the render should include indirect volume lighting contributions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting Components", DisplayName = "Indirect Volume", meta = (EditCondition = "bOverride_bLightingComponents_IncludeIndirectVolume"))
	bool bLightingComponents_IncludeIndirectVolume = true;

	/**
	* An array of additional post-processing materials to run after the frame is rendered. Using this feature may add a notable amount of render time.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post Process Materials", meta=(EditCondition="bOverride_AdditionalPostProcessMaterials"))
	TArray<FMoviePipelinePostProcessPass> AdditionalPostProcessMaterials;

private:
	/**
	 * The original value of the "r.PathTracing.ProgressDisplay" cvar before the render starts. The progress display
	 * will be hidden during the render.
	 */
	bool bOriginalProgressDisplayCvarValue = false;

	/**
	 * The original value of the "r.NFOR.FrameCount" cvar before the render starts. Will use the new value set in
	 * this node during the render.
	 */
	int32 OriginalFrameCountCvarValue = 2;

	/**
	 * The original value of the "r.PathTracing.SpatialDenoiser.Type" cvar before the render starts. Will use the new value set in
	 * this node during the render.
	 */
	int32 OriginalDenoiserType = 0;
};