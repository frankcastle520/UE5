// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


#include "LandscapePatchComponent.h"
#include "LandscapeTextureBackedRenderTarget.h"

#include "LandscapeTexturePatch.generated.h"

class ULandscapeTexturePatch;

class ULandscapeHeightTextureBackedRenderTarget;
class UTexture;
class UTexture2D;

/**
 * Determines where the patch gets its information, which affects its memory usage in editor (not in runtime,
 * since patches are baked directly into landscape and removed for runtime).
 */
UENUM(BlueprintType)
enum class ELandscapeTexturePatchSourceMode : uint8
{
	/**
	 * The patch is considered not to have any data stored for this element. Setting source mode to this is
	 * a way to discard any internally stored data.
	 */
	None,

	/**
	 * The data will be read from an internally-stored UTexture2D. In this mode, the patch can't be written-to via
	 * blueprints, but it avoids storing the extra render target needed for TextureBackedRenderTarget.
	 */
	InternalTexture,

	/**
	* The patch data will be read from an internally-stored render target, which can be written to via Blueprints
	* and which gets serialized to an internally stored UTexture2D when needed. Uses double the memory of InternalTexture.
	*/
	TextureBackedRenderTarget,

	/**
	 * The data will be read from a UTexture asset (which can be a render target). Allows multiple patches
	 * to share the same texture.
	 */
	 TextureAsset
};

// Determines how the patch is combined with the previous state of the landscape.
UENUM(BlueprintType)
enum class ELandscapeTexturePatchBlendMode : uint8
{
	// Let the patch specify the actual target height, and blend that with the existing
	// height using falloff/alpha. E.g. with no falloff and alpha 1, the landscape will
	// be set directly to the height sampled from patch. With alpha 0.5, landscape height 
	// will be averaged evenly with patch height.
	AlphaBlend = static_cast<uint8>(UE::Landscape::FApplyLandscapeTextureHeightPatchPS::EBlendMode::AlphaBlend),

	// Interpreting the landscape mid value as 0, use the texture patch as an offset to
	// apply to the landscape. Falloff/alpha will just affect the degree to which the offset
	// is applied (e.g. alpha of 0.5 will apply just half the offset).
	Additive = static_cast<uint8>(UE::Landscape::FApplyLandscapeTextureHeightPatchPS::EBlendMode::Additive),

	// Like Alpha Blend mode, but limited to only lowering the existing landscape values
	Min = static_cast<uint8>(UE::Landscape::FApplyLandscapeTextureHeightPatchPS::EBlendMode::Min),

	// Like Alpha Blend mode, but limited to only raising the existing landscape values
	Max = static_cast<uint8>(UE::Landscape::FApplyLandscapeTextureHeightPatchPS::EBlendMode::Max)
};

// Determines falloff method for the patch's influence.
UENUM(BlueprintType)
enum class ELandscapeTexturePatchFalloffMode : uint8
{
	// Affect landscape in a circle inscribed in the patch, and fall off across
	// a margin extending into that circle.
	Circle,

	// Affect entire rectangle of patch (except for circular corners), and fall off
	// across a margin extending inward from the boundary.
	RoundedRectangle,
};

UENUM(BlueprintType)
enum class ELandscapeTextureHeightPatchEncoding : uint8
{
	// Values in texture should be interpreted as being floats in the range [0,1]. User specifies what
	// value corresponds to height 0 (i.e. height when landscape is "cleared"), and the size of the 
	// range in world units.
	ZeroToOne,

	// Values in texture are direct world-space heights.
	WorldUnits,

	// Values in texture are stored the same way they are in landscape actors: as 16 bit integers packed 
	// into two bytes, mapping to [-256, 256 - 1/128] before applying landscape scale.
	NativePackedHeight

	//~ Note that currently ZeroToOne and WorldUnits actually work the same way- we subtract the center point (0 for WorldUnits),
	//~ then scale in some way (1.0 for WorldUnits). However, having separate options here allows us to initialize defaults
	//~ appropriately when setting the encoding mode via ResetSourceEncodingMode.
};

UENUM(BlueprintType)
enum class ELandscapeTextureHeightPatchZeroHeightMeaning : uint8
{
	// Zero height corresponds to the patch vertical position relative to the landscape. This moves
	// the results up and down as the patch moves up and down.
	PatchZ,

	// Zero height corresponds to Z = 0 in the local space of the landscape, regardless of the patch vertical
	// position. For instance, if landscape transform has z=-100 in world, then writing height 0 will correspond
	// to z=-100 in world coordinates, regardless of patch Z. 
	LandscapeZ,

	// Zero height corresponds to the height of the world origin relative to landscape. In other words, writing
	// height 0 will correspond to world z = 0 regardless of patch Z or landscape transform (as long as landscape
	// transform still has Z up in world coordinates).
	WorldZero
};

//~ A struct in case we find that we need other encoding settings.
USTRUCT(BlueprintType)
struct LANDSCAPEPATCH_API FLandscapeTexturePatchEncodingSettings
{
	GENERATED_BODY()
public:
	/**
	 * The value in the patch data that corresponds to 0 height relative to the starting point
	 * specified by Zero Height Meaning.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	double ZeroInEncoding = 0;

	/**
	 * The scale that should be aplied to the data stored in the patch relative to the zero in the encoding, in world coordinates.
	 * For instance if the encoding is [0,1], and 0.5 correponds to 0, a WorldSpaceEncoding Scale of 100 means that the resulting
	 * values will lie in the range [-50, 50] in world space, which would be [-0.5, 0.5] in the landscape local heights if the Z
	 * scale is 100.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	double WorldSpaceEncodingScale = 1;
};

//~ Ideally this would be a nested class, but it needs to be a UObject, which can't be nested.
/**
 * Helper class for ULandscapeTexturePatch that stores information for a given weight layer.
 * Should not be used outside this class.
 */
UCLASS(EditInlineNew, CollapseCategories)
class LANDSCAPEPATCH_API ULandscapeWeightPatchTextureInfo : public UObject
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	// UObject
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PreDuplicate(FObjectDuplicationParameters& DupParams) override;
#endif // WITH_EDITOR

protected:
	UPROPERTY(EditAnywhere, Category = WeightPatch)
	FName WeightmapLayerName;

	/** Specifies if this patch edits the visibility layer. */
	UPROPERTY(EditAnywhere, Category = WeightPatch)
	bool bEditVisibilityLayer = false;
	
	/** Texture to use when source mode is set to texture asset. */
	UPROPERTY(EditAnywhere, Category = WeightPatch, meta = (EditConditionHides,
		EditCondition = "SourceMode == ELandscapeTexturePatchSourceMode::TextureAsset", 
		DisallowedAssetDataTags = "VirtualTextureStreaming=True"))
	TObjectPtr<UTexture> TextureAsset = nullptr;

	/** Not directly settable via detail panel- for display/debugging purposes only. */
	UPROPERTY(VisibleAnywhere, Category = WeightPatch, Instanced, AdvancedDisplay)
	TObjectPtr<ULandscapeWeightTextureBackedRenderTarget> InternalData = nullptr;

	UPROPERTY(EditAnywhere, Category = WeightPatch, meta = (EditConditionHides, EditCondition = "false"))
	ELandscapeTexturePatchSourceMode SourceMode = ELandscapeTexturePatchSourceMode::None;

	/**
	 * How the heightmap of the patch is stored.
	 */
	UPROPERTY(EditAnywhere, Category = WeightPatch, meta = (DisplayName = "Source Mode"))
	ELandscapeTexturePatchSourceMode DetailPanelSourceMode = ELandscapeTexturePatchSourceMode::None;

	//~ We could refactor things such that we always have an InternalData pointer, even when we use
	//~ a texture asset, and then we could use the boolean inside that instead (which needs to be there
	//~ so that we know how many channels we need). Not clear whether that will be any cleaner though.
	UPROPERTY(EditAnywhere, Category = WeightPatch)
	bool bUseAlphaChannel = false;

	// Can't make TOptional a UPROPERTY, hence these two.
	UPROPERTY(EditAnywhere, Category = WeightPatch)
	bool bOverrideBlendMode = false;
	UPROPERTY(EditAnywhere, Category = WeightPatch, meta = (EditConditionHides, EditCondition = "bOverrideBlendMode"))
	ELandscapeTexturePatchBlendMode OverrideBlendMode = ELandscapeTexturePatchBlendMode::AlphaBlend;

	// TODO: We could support having different per-layer falloff modes and falloff amounts as well, as
	// additional override members. But probably better to wait to see if that is actually desired.

	bool bReinitializeOnNextRender = false;

	void SetSourceMode(ELandscapeTexturePatchSourceMode NewMode);
#if WITH_EDITOR
	void TransitionSourceModeInternal(ELandscapeTexturePatchSourceMode OldMode, ELandscapeTexturePatchSourceMode NewMode);
#endif

	friend class ULandscapeTexturePatch;
};


UCLASS(Blueprintable, BlueprintType, ClassGroup = Landscape, meta = (BlueprintSpawnableComponent))
class LANDSCAPEPATCH_API ULandscapeTexturePatch : public ULandscapePatchComponent
{
	GENERATED_BODY()

public:

#if WITH_EDITOR
	using FEditLayerTargetTypeState = UE::Landscape::EditLayers::FEditLayerTargetTypeState;
	using FEditLayerRenderItem = UE::Landscape::EditLayers::FEditLayerRenderItem;

	// ILandscapeEditLayerRenderer, via ULandscapePatchComponent
	virtual void GetRendererStateInfo(const ULandscapeInfo* InLandscapeInfo,
		FEditLayerTargetTypeState& OutSupportedTargetTypeState,
		FEditLayerTargetTypeState& OutEnabledTargetTypeState,
		TArray<TSet<FName>>& OutRenderGroups) const override;
	virtual FString GetEditLayerRendererDebugName() const override;
	virtual TArray<FEditLayerRenderItem> GetRenderItems(const ULandscapeInfo* InLandscapeInfo) const override;
	virtual void RenderLayer(FRenderParams& InRenderParams) override;

	// ULandscapePatchComponent
	UTextureRenderTarget2D* RenderLayer_Native(const FLandscapeBrushParameters& InParameters, const FTransform& HeightmapToWorld) override;
	virtual bool CanAffectHeightmap() const override;
	virtual bool CanAffectWeightmap() const override;
	virtual bool CanAffectWeightmapLayer(const FName& InLayerName) const override;
	virtual bool CanAffectVisibilityLayer() const override;
	virtual void GetRenderDependencies(TSet<UObject*>& OutDependencies) const override;

	// UActorComponent
	virtual TStructOnScope<FActorComponentInstanceData> GetComponentInstanceData() const override;

	// UObject
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/**
	 * Gets the transform from patch to world. The transform is based off of the component
	 * transform, but with rotation changed to align to the landscape, only using the yaw
	 * to rotate it relative to the landscape.
	 */
	UFUNCTION(BlueprintCallable, Category = LandscapePatch)
	virtual FTransform GetPatchToWorldTransform() const;

	/**
	 * Gives size in unscaled world coordinates (ie before applying patch transform) of the patch as measured 
	 * between the centers of the outermost pixels. Measuring the coverage this way means that a patch can 
	 * affect the same region of the landscape regardless of patch resolution.
	 * This is also the range across which bilinear interpolation always has correct values, so the area outside 
	 * this center portion is usually set as a "dead" border that doesn't affect the landscape.
	 */
	UFUNCTION(BlueprintCallable, Category = LandscapePatch)
	virtual FVector2D GetUnscaledCoverage() const { return FVector2D(UnscaledPatchCoverage); }

	/**
	 * Set the patch coverage (see GetUnscaledCoverage for description).
	 */
	UFUNCTION(BlueprintCallable, Category = LandscapePatch)
	virtual void SetUnscaledCoverage(FVector2D Coverage) { UnscaledPatchCoverage = Coverage; }

	/**
	 * Gives size in unscaled world coordinates of the patch in the world, based off of UnscaledCoverage and
	 * texture resolution (i.e., adds a half-pixel around UnscaledCoverage).
	 */
	UFUNCTION(BlueprintCallable, Category = LandscapePatch)
	virtual FVector2D GetFullUnscaledWorldSize() const;

	/**
	 * Gets the size (in pixels) of the internal textures used by the patch. Does not reflect the resolution
	 * of any used texture assets (if the source mode is texture asset).
	 */
	UFUNCTION(BlueprintCallable, Category = LandscapePatch)
	virtual FVector2D GetResolution() const { return FVector2D(ResolutionX, ResolutionY); }

	/**
	 * Sets the resolution of the currently used internal texture or render target. Has no effect
	 * if the source mode is set to an external texture asset.
	 *
	 * @return true if successful.
	 */
	UFUNCTION(BlueprintCallable, Category = LandscapePatch)
	virtual void SetResolution(FVector2D ResolutionIn);

	/**
	 * Given the landscape resolution, current patch coverage, and a landscape resolution multiplier, gives the
	 * needed resolution of the landscape patch. I.e., figures out the number of pixels in the landcape that
	 * would be in a region of such size, and then uses the resolution multiplier to give a result.
	 *
	 * @return true if successful (may fail if landscape is not set, for instance)
	 */
	UFUNCTION(BlueprintCallable, Category = LandscapePatch, meta = (ResolutionMultiplier = "1.0"))
	virtual UPARAM(DisplayName = "Success") bool GetInitResolutionFromLandscape(float ResolutionMultiplier, FVector2D& ResolutionOut) const;


	UFUNCTION(BlueprintCallable, Category = LandscapePatch)
	void SetFalloff(float FalloffIn) 
	{
		if (Falloff != FalloffIn)
		{
			Modify();
			Falloff = FalloffIn;
		}
	}

	UFUNCTION(BlueprintCallable, Category = LandscapePatch)
	void SetFalloffMode(ELandscapeTexturePatchFalloffMode FalloffModeIn) 
	{
		if (FalloffMode != FalloffModeIn)
		{
			Modify();
			FalloffMode = FalloffModeIn;
		}
	}

	/**
	 * Determines how the height patch is blended into the existing terrain.
	 */
	UFUNCTION(BlueprintCallable, Category = LandscapePatch)
	void SetBlendMode(ELandscapeTexturePatchBlendMode BlendModeIn) 
	{
		if (BlendMode != BlendModeIn)
		{
			Modify();
			BlendMode = BlendModeIn;
		}
	}


	// Height related functions:

	UFUNCTION(BlueprintCallable, Category = LandscapePatch)
	virtual ELandscapeTexturePatchSourceMode GetHeightSourceMode() const { return HeightSourceMode; }

	/**
	 * Changes source mode. There are currently no API guarantees regarding the initialization of the
	 * new source data. E.g. when first switching to use an internal render target, the data in that
	 * render target may not be initialized.
	 */
	UFUNCTION(BlueprintCallable, Category = LandscapePatch)
	virtual void SetHeightSourceMode(ELandscapeTexturePatchSourceMode NewMode);

	/**
	 * Sets the texture used for height when the height source mode is set to texture asset. Note that
	 * virtual textures are not supported.
	 */
	UFUNCTION(BlueprintCallable, Category = LandscapePatch)
	void SetHeightTextureAsset(UTexture* TextureIn);

	/**
	 * Gets the internal height render target, if source mode is set to Texture Backed Render Target.
	 * 
	 * Things that should be set up if using the internal render target:
	 * - SetHeightSourceMode should have been called with TextureBackedRenderTarget.
	 * - An appropriate texture size should have been set with SetResolution. If the patch extent has already
	 *  been set, you can base your resolution on the extent and the resolution of the landscape by using
	 *  GetInitResolutionFromLandscape().
	 * - SetHeightRenderTargetFormat should have been called with a desired format. In particular, if using
	 *  an alpha channel, the format should have an alpha channel (and SetUseAlphaChannelForHeight should have
	 *  been called with "true").
	 * 
	 * In addition, you may need to call SetHeightEncodingMode, SetHeightEncodingSettings, and SetZeroHeightMeaning
	 * based on how you want the data you write to be interpreted. This part is not specific to using an internal render
	 * target, since you are likely to need to do that with a TextureAsset source mode as well.
	 * 
	 * @param bMarkDirty If true, marks the containing package as dirty, since the render target is presumably
	 *  being written to. Can be set to false if the render target is not being written to.
	 */
	UFUNCTION(BlueprintCallable, Category = LandscapePatch)
	virtual UTextureRenderTarget2D* GetHeightRenderTarget(bool bMarkDirty = true);

	UFUNCTION(BlueprintCallable, Category = LandscapePatch, meta = (ETextureRenderTargetFormat = "ETextureRenderTargetFormat::RTF_R32f"))
	void SetHeightRenderTargetFormat(ETextureRenderTargetFormat Format);

	/**
	 * Determines whether the height patch alpha channel is used for blending into the existing values.
	 * Note that the source data needs to have an alpha channel in this case. How the alpha channel is
	 * used depends on the patch blend mode (see SetBlendMode).
	 */
	UFUNCTION(BlueprintCallable, Category = "LandscapePatch")
	void SetUseAlphaChannelForHeight(bool bUse)
	{
		if (bUseTextureAlphaForHeight != bUse)
		{
			Modify();
			bUseTextureAlphaForHeight = bUse;
		}
	}

	/**
	 * Set the height encoding mode for the patch, which determines how stored values in the patch
	 * are translated into heights when applying to landscape.
	 */
	UFUNCTION(BlueprintCallable, Category = LandscapePatch)
	void SetHeightEncodingMode(ELandscapeTextureHeightPatchEncoding EncodingMode)
	{
		if (HeightEncoding != EncodingMode)
		{
			Modify();
			HeightEncoding = EncodingMode;
		}
	}

	/**
	 * Just like SetSourceEncodingMode, but resets ZeroInEncoding, WorldSpaceEncodingScale, and height
	 * render target format to mode-specific defaults.
	 */
	UFUNCTION(BlueprintCallable, Category = LandscapePatch)
	void ResetHeightEncodingMode(ELandscapeTextureHeightPatchEncoding EncodingMode);

	/**
	 * Set settings that determine how values in the patch are translated into heights. This is only
	 * used if the encoding mode is not NativePackedHeight, where values are expected to be already
	 * in the same space as the landscape heightmap.
	 */
	UFUNCTION(BlueprintCallable, Category = LandscapePatch)
	void SetHeightEncodingSettings(const FLandscapeTexturePatchEncodingSettings& Settings);

	/**
	 * Set how zero height is interpreted, see comments in ELandscapeTextureHeightPatchZeroHeightMeaning.
	 */
	UFUNCTION(BlueprintCallable, Category = "LandscapePatch")
	void SetZeroHeightMeaning(ELandscapeTextureHeightPatchZeroHeightMeaning ZeroHeightMeaningIn)
	{ 
		if (ZeroHeightMeaning != ZeroHeightMeaningIn)
		{
			Modify();
			ZeroHeightMeaning = ZeroHeightMeaningIn;
		}
	}


	// Weight related functions:

	/**
	 * By default, the layer is added with source mode set to be a texture-backed render target.
	 */
	UFUNCTION(BlueprintCallable, Category = LandscapePatch)
	virtual void AddWeightPatch(const FName& InWeightmapLayerName, ELandscapeTexturePatchSourceMode SourceMode, bool bUseAlphaChannel);

	UFUNCTION(BlueprintCallable, Category = LandscapePatch)
	virtual void RemoveWeightPatch(const FName& InWeightmapLayerName);

	UFUNCTION(BlueprintCallable, Category = LandscapePatch)
	virtual void RemoveAllWeightPatches();

	/** Sets the soure mode of all weight patches to "None". */
	UFUNCTION(BlueprintCallable, Category = LandscapePatch)
	virtual void DisableAllWeightPatches();

	UFUNCTION(BlueprintCallable, Category = LandscapePatch)
	TArray<FName> GetAllWeightPatchLayerNames();
	
	UFUNCTION(BlueprintCallable, Category = LandscapePatch)
	virtual ELandscapeTexturePatchSourceMode GetWeightPatchSourceMode(const FName& InWeightmapLayerName);

	UFUNCTION(BlueprintCallable, Category = LandscapePatch)
	virtual void SetWeightPatchSourceMode(const FName& InWeightmapLayerName, ELandscapeTexturePatchSourceMode NewMode);

	UFUNCTION(BlueprintCallable, Category = LandscapePatch)
	void SetWeightPatchTextureAsset(const FName& InWeightmapLayerName, UTexture* TextureIn);

	/**
	 * @param bMarkDirty If true, marks the containing package as dirty, since the render target is presumably
	 *  being written to. Can be set to false if the render target is not being written to.
	 */
	UFUNCTION(BlueprintCallable, Category = LandscapePatch)
	virtual UTextureRenderTarget2D* GetWeightPatchRenderTarget(const FName& InWeightmapLayerName, bool bMarkDirty = true);

	UFUNCTION(BlueprintCallable, Category = LandscapePatch)
	virtual void SetUseAlphaChannelForWeightPatch(const FName& InWeightmapLayerName, bool bUseAlphaChannel);

	UFUNCTION(BlueprintCallable, Category = LandscapePatch)
	virtual void SetWeightPatchBlendModeOverride(const FName& InWeightmapLayerName, ELandscapeTexturePatchBlendMode BlendMode);

	UFUNCTION(BlueprintCallable, Category = LandscapePatch)
	virtual void ClearWeightPatchBlendModeOverride(const FName& InWeightmapLayerName);

	UFUNCTION(BlueprintCallable, Category = LandscapePatch)
	virtual void SetEditVisibilityLayer(const FName& InWeightmapLayerName, const bool bEditVisibilityLayer);

protected:
	//~ Don't expose these on the instance because a user might not realize that they would lose their existing internal
	//~ data by dragging them, and the only way they can reinitialize data in the viewport is through the methods that
	//~ already use InitTextureSizeX/Y as inputs
	UPROPERTY(EditDefaultsOnly, Category = Settings)
	int32 ResolutionX = 32;
	UPROPERTY(EditDefaultsOnly, Category = Settings)
	int32 ResolutionY = 32;

	/** At scale 1.0, the X and Y of the region affected by the height patch. This corresponds to the distance from the center
	 of the first pixel to the center of the last pixel in the patch texture in the X and Y directions. */
	UPROPERTY(EditAnywhere, Category = Settings, meta = (UIMin = "0", ClampMin = "0"))
	FVector2D UnscaledPatchCoverage = FVector2D(2000, 2000);

	UPROPERTY(EditAnywhere, Category = Settings)
	ELandscapeTexturePatchBlendMode BlendMode = ELandscapeTexturePatchBlendMode::AlphaBlend;

	UPROPERTY(EditAnywhere, Category = Settings)
	ELandscapeTexturePatchFalloffMode FalloffMode = ELandscapeTexturePatchFalloffMode::RoundedRectangle;

	/**
	 * Distance (in unscaled world coordinates) across which to smoothly fall off the patch effects.
	 */
	UPROPERTY(EditAnywhere, Category = Settings, meta = (ClampMin = "0", UIMax = "2000"))
	float Falloff = 0;


	// Height properties:

	// How the heightmap of the patch is stored. This is the property that is actually used, and it will
	// agree with DetailPanelHeightSourceMode at all times except when user is changing the latter via the
	// detail panel.
	//~ TODO: The property specifiers here are a hack to force this (hidden) property to be preserved across reruns of
	//~ a construction script in a blueprint actor. We should find the proper way that this is supposed to be done.
	UPROPERTY(EditAnywhere, Category = HeightPatch, meta = (EditConditionHides, EditCondition = "false"))
	ELandscapeTexturePatchSourceMode HeightSourceMode = ELandscapeTexturePatchSourceMode::None;

	/**
	 * How the heightmap of the patch is stored.
	 */
	UPROPERTY(EditAnywhere, Category = HeightPatch, meta = (DisplayName = "Source Mode"))
	ELandscapeTexturePatchSourceMode DetailPanelHeightSourceMode = ELandscapeTexturePatchSourceMode::None;

	/** Not directly settable via detail panel- for display/debugging purposes only. */
	UPROPERTY(VisibleAnywhere, Category = HeightPatch, Instanced)
	TObjectPtr<ULandscapeHeightTextureBackedRenderTarget> HeightInternalData = nullptr;

	/**
	 * Texture used when source mode is set to a texture asset.
	 */
	UPROPERTY(EditAnywhere, Category = HeightPatch, meta = (EditConditionHides,
		EditCondition = "HeightSourceMode == ELandscapeTexturePatchSourceMode::TextureAsset", 
		DisallowedAssetDataTags = "VirtualTextureStreaming=True"))
	TObjectPtr<UTexture> HeightTextureAsset = nullptr;

	
	/** 
	 * When true, texture alpha channel will be used when applying the patch. Note that the source data needs to
	 * have an alpha channel for this to have an effect.
	 */
	UPROPERTY(EditAnywhere, Category = HeightPatch)
	bool bUseTextureAlphaForHeight = false;

	/** How the values stored in the patch represent the height. Not customizable for Internal Texture source mode, which always uses native packed height. */
	UPROPERTY(EditAnywhere, Category = HeightPatch, meta = (
		EditCondition = "HeightSourceMode != ELandscapeTexturePatchSourceMode::InternalTexture"))
	ELandscapeTextureHeightPatchEncoding HeightEncoding = ELandscapeTextureHeightPatchEncoding::WorldUnits;

	/** Encoding settings. Not relevant when using native packed height as the encoding. */
	UPROPERTY(EditAnywhere, Category = HeightPatch, meta = (UIMin = "0", UIMax = "1",
		EditCondition = "HeightSourceMode != ELandscapeTexturePatchSourceMode::InternalTexture && HeightEncoding != ELandscapeTextureHeightPatchEncoding::NativePackedHeight"))
	FLandscapeTexturePatchEncodingSettings HeightEncodingSettings;

	/**
	 * How 0 height is interpreted.
	 */
	UPROPERTY(EditAnywhere, Category = HeightPatch)
	ELandscapeTextureHeightPatchZeroHeightMeaning ZeroHeightMeaning = ELandscapeTextureHeightPatchZeroHeightMeaning::PatchZ;

	/**
	 * Whether to apply the patch Z scale to the height stored in the patch.
	 */
	UPROPERTY(EditAnywhere, Category = HeightPatch, AdvancedDisplay, meta = (DisplayName = "Apply Component Z Scale"))
	bool bApplyComponentZScale = true;


	// Weight properties:

	/** 
	 * Weight patches. 
	 * Note that manipulating these in the blueprint editor will not reliably update instances that are already
	 * placed into the world, due to current limitations in how change detection is done for such arrays. Specifically,
	 * existing instances that are actually not customized are very likely to be erroneously be treated as having
	 * customized their version of the array, causing the blueprint changes to not be pushed to those instances
	 * when they otherwise would be for most other properties.
	 */
	UPROPERTY(EditAnywhere, Category = WeightPatches, Instanced, NoClear, meta=(NoResetToDefault))
	TArray<TObjectPtr<ULandscapeWeightPatchTextureInfo>> WeightPatches;

	// Reinitialization from detail panel:

	/**
	 * Given the current initialization settings, reinitialize the height patch.
	 */
	UFUNCTION(CallInEditor, Category = HeightPatch, meta = (DisplayName= "Reinitialize Height"))
	void RequestReinitializeHeight();

	UFUNCTION(CallInEditor, Category = WeightPatches, meta = (DisplayName = "Reinitialize Weights"))
	void RequestReinitializeWeights();

	bool bReinitializeHeightOnNextRender = false;

	/**
	 * Adjusts patch rotation to be aligned to a 90 degree increment relative to the landscape,
	 * adjusts UnscaledPatchCoverage such that it becomes a multiple of landscape quad size, and
	 * adjusts patch location so that the boundaries of the covered area lie on the nearest
	 * landscape vertices.
	 * Note that this doesn't adjust the resolution of the texture that the patch uses, so landscape
	 * vertices within the inside of the patch may still not always align with texture patch pixel
	 * centers (if the resolutions aren't multiples of each other).
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = Initialization)
	void SnapToLandscape();

	/** When initializing from landscape, set resolution based off of the landscape (and a multiplier). */
	UPROPERTY(EditAnywhere, Category = Initialization)
	bool bBaseResolutionOffLandscape = true;

	/** 
	 * Multiplier to apply to landscape resolution when initializing patch resolution. A value greater than 1.0 will use higher
	 * resolution than the landscape (perhaps useful for slightly more accurate results while not aligned to landscape), and
	 * a value less that 1.0 will use lower.
	 */
	UPROPERTY(EditAnywhere, Category = Initialization, meta = (
		EditCondition = "bBaseResolutionOffLandscape"))
	float ResolutionMultiplier = 1;

	/** Texture width to use when reinitializing using Reinitialize Weights or ReinitializeHeight, if not basing resolution off landscape. */
	UPROPERTY(EditAnywhere, Category = Initialization, meta = (EditCondition = "!bBaseResolutionOffLandscape", 
		ClampMin = "1"))
	int32 InitTextureSizeX = 33;

	/** Texture height to use when reinitializing using Reinitialize Weights or ReinitializeHeight, if not basing resolution off landscape. */
	UPROPERTY(EditAnywhere, Category = Initialization, meta = (EditCondition = "!bBaseResolutionOffLandscape", 
		ClampMin = "1"))
	int32 InitTextureSizeY = 33;

private:
	UTexture2D* GetHeightInternalTexture();
	UTextureRenderTarget2D* GetWeightPatchRenderTarget(ULandscapeWeightPatchTextureInfo* WeightPatch);
	UTexture2D* GetWeightPatchInternalTexture(ULandscapeWeightPatchTextureInfo* WeightPatch);

	void UpdateHeightConvertToNativeParamsIfNeeded();
#if WITH_EDITOR
	void TransitionHeightSourceModeInternal(ELandscapeTexturePatchSourceMode OldMode, ELandscapeTexturePatchSourceMode NewMode);
	FLandscapeHeightPatchConvertToNativeParams GetHeightConvertToNativeParams() const;
	UTextureRenderTarget2D* ApplyToHeightmap(UTextureRenderTarget2D* InCombinedResult, const FTransform& LandscapeHeightmapToWorld);
	void ApplyToWeightmap(ULandscapeWeightPatchTextureInfo* PatchInfo,
		TFunction<FRHITexture* ()> RenderThreadLandscapeTextureGetter, int32 LandscapeTextureSliceIndex,
		const FIntPoint& LandscapeTextureResolution, const FTransform& LandscapeHeightmapToWorld);

	void GetCommonShaderParams(const FTransform& LandscapeHeightmapToWorldIn, 
		const FIntPoint& SourceResolutionIn, const FIntPoint& DestinationResolutionIn,
		FTransform& PatchToWorldOut, FVector2f& PatchWorldDimensionsOut, FMatrix44f& HeightmapToPatchOut, 
		FIntRect& DestinationBoundsOut, FVector2f& EdgeUVDeadBorderOut, float& FalloffWorldMarginOut) const;
	void GetHeightShaderParams(const FTransform& LandscapeHeightmapToWorldIn, 
		const FIntPoint& SourceResolutionIn, const FIntPoint& DestinationResolutionIn,
		UE::Landscape::FApplyLandscapeTextureHeightPatchPS::FParameters& ParamsOut, FIntRect& DestinationBoundsOut) const;
	void GetWeightShaderParams(const FTransform& LandscapeHeightmapToWorldIn, const FIntPoint& SourceResolutionIn, 
		const FIntPoint& DestinationResolutionIn, const ULandscapeWeightPatchTextureInfo* WeightPatchInfo, 
		UE::Landscape::FApplyLandscapeTextureWeightPatchPS::FParameters& ParamsOut, FIntRect& DestinationBoundsOut) const;
	FMatrix44f GetPatchToHeightmapUVs(const FTransform& LandscapeHeightmapToWorld, int32 PatchSizeX, int32 PatchSizeY, int32 HeightmapSizeX, int32 HeightmapSizeY) const;
	void ReinitializeHeight(UTextureRenderTarget2D* InCombinedResult, const FTransform& LandscapeHeightmapToWorld);
	/**
	 * @param SliceIndex set to a negative value when not using a Texture2DArray
	 */
	void ReinitializeWeightPatch(ULandscapeWeightPatchTextureInfo* PatchInfo,
		FTextureResource* InputResource, FIntPoint ResourceSize, int32 SliceIndex,
		const FTransform& LandscapeHeightmapToWorld);

	bool WeightPatchCanRender(const ULandscapeWeightPatchTextureInfo& InWeightPatch) const;

	void ResetHeightRenderTargetFormat();
#endif // WITH_EDITOR

	UPROPERTY(EditDefaultsOnly, Category = Settings)
	TEnumAsByte<ETextureRenderTargetFormat> HeightRenderTargetFormat = ETextureRenderTargetFormat::RTF_R32f;
};

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_2
#include "CoreMinimal.h"
#endif
