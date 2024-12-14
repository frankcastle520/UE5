// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LandscapeComponent.h"
#include "UObject/ObjectMacros.h"
#include "LandscapeProxy.h"
#include "LandscapeBlueprintBrushBase.h"
#include "LandscapeEditTypes.h"
#include "LandscapeEditLayerRenderer.h"
#include "Delegates/DelegateCombinations.h"
#include "Templates/SubclassOf.h"
#include "Algo/Transform.h"
#include "LandscapeLayerInfoObject.h"

#include "Landscape.generated.h"

class FTextureRenderTargetResource;
class ULandscapeComponent;
class ILandscapeEdModeInterface;
class SNotificationItem;
class UStreamableRenderAsset;
class UTextureRenderTarget;
class ULandscapeEditLayerBase;
class FMaterialResource;
struct FLandscapeEditLayerComponentReadbackResult;
struct FLandscapeNotification;
struct FTextureToComponentHelper;
struct FUpdateLayersContentContext;
struct FEditLayersHeightmapMergeParams;
struct FEditLayersWeightmapMergeParams;
enum class ELandscapeNotificationType;

namespace EditLayersHeightmapLocalMerge_RenderThread
{
	struct FMergeInfo;
}

namespace EditLayersWeightmapLocalMerge_RenderThread
{
	struct FMergeInfo;
}

#if WITH_EDITOR
extern LANDSCAPE_API TAutoConsoleVariable<int32> CVarLandscapeSplineFalloffModulation;
#endif

UENUM()
enum class ERTDrawingType : uint8
{
	RTAtlas,
	RTAtlasToNonAtlas,
	RTNonAtlasToAtlas,
	RTNonAtlas,
	RTMips
};

UENUM()
enum class EHeightmapRTType : uint8
{
	HeightmapRT_CombinedAtlas,
	HeightmapRT_CombinedNonAtlas,
	HeightmapRT_Scratch1,
	HeightmapRT_Scratch2,
	HeightmapRT_Scratch3,
	HeightmapRT_BoundaryNormal, // HACK [chris.tchou] remove once we have a better boundary normal solution
	// Mips RT
	HeightmapRT_Mip1,
	HeightmapRT_Mip2,
	HeightmapRT_Mip3,
	HeightmapRT_Mip4,
	HeightmapRT_Mip5,
	HeightmapRT_Mip6,
	HeightmapRT_Mip7,
	HeightmapRT_Count
};

UENUM()
enum class EWeightmapRTType : uint8
{
	WeightmapRT_Scratch_RGBA,
	WeightmapRT_Scratch1,
	WeightmapRT_Scratch2,
	WeightmapRT_Scratch3,

	// Mips RT
	WeightmapRT_Mip0,
	WeightmapRT_Mip1,
	WeightmapRT_Mip2,
	WeightmapRT_Mip3,
	WeightmapRT_Mip4,
	WeightmapRT_Mip5,
	WeightmapRT_Mip6,
	WeightmapRT_Mip7,
	
	WeightmapRT_Count
};

#if WITH_EDITOR
enum ELandscapeLayerUpdateMode : uint32;
#endif

USTRUCT()
struct FLandscapeLayerBrush
#if CPP && WITH_EDITOR // UHT doesn't support inheriting from namespaced class
	: public UE::Landscape::EditLayers::IEditLayerRendererProvider
#endif // CPP && WITH_EDITOR
{
	GENERATED_USTRUCT_BODY()

	FLandscapeLayerBrush() = default;

	FLandscapeLayerBrush(ALandscapeBlueprintBrushBase* InBlueprintBrush)
#if WITH_EDITORONLY_DATA
		: BlueprintBrush(InBlueprintBrush)
#endif // WITH_EDITORONLY_DATA
	{}

	virtual ~FLandscapeLayerBrush() = default;

#if WITH_EDITOR
	UTextureRenderTarget2D* RenderLayer(const FLandscapeBrushParameters& InParameters);

	LANDSCAPE_API ALandscapeBlueprintBrushBase* GetBrush() const;
	bool Affects(ELandscapeToolTargetType InTargetType, const FName& InWeightmapLayerName) const;
	bool AffectsHeightmap() const;
	bool AffectsWeightmapLayer(const FName& InWeightmapLayerName) const;
	bool AffectsVisibilityLayer() const;
	void SetOwner(ALandscape* InOwner);

	//~ Begin UE::Landscape::EditLayers::IEditLayerRendererProvider implementation
	LANDSCAPE_EDIT_LAYERS_BATCHED_MERGE_EXPERIMENTAL
	LANDSCAPE_API virtual TArray<UE::Landscape::EditLayers::FEditLayerRendererState> GetEditLayerRendererStates(const ULandscapeInfo* InLandscapeInfo, bool bInSkipBrush) override;
	//~ End UE::Landscape::EditLayers::IEditLayerRendererProvider implementation
#endif // WITH_EDITOR

private:

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<ALandscapeBlueprintBrushBase> BlueprintBrush;
#endif // WITH_EDITORONLY_DATA
};

UENUM()
enum ELandscapeBlendMode : int
{
	LSBM_AdditiveBlend,
	LSBM_AlphaBlend,
	LSBM_MAX,
};

USTRUCT()
struct FLandscapeLayer
#if CPP && WITH_EDITOR // UHT doesn't support inheriting from namespaced class
	: public UE::Landscape::EditLayers::IEditLayerRendererProvider
#endif // CPP && WITH_EDITOR
{
	GENERATED_USTRUCT_BODY()

	FLandscapeLayer()
		: Guid(FGuid::NewGuid())
		, Name(NAME_None)
		, bVisible(true)
		, bLocked(false)
		, HeightmapAlpha(1.0f)
		, WeightmapAlpha(1.0f)
		, BlendMode(LSBM_AdditiveBlend)
	{}

	virtual ~FLandscapeLayer() = default;

#if WITH_EDITOR
	//~ Begin UE::Landscape::EditLayers::IEditLayerRendererProvider implementation
	LANDSCAPE_EDIT_LAYERS_BATCHED_MERGE_EXPERIMENTAL
	LANDSCAPE_API virtual TArray<UE::Landscape::EditLayers::FEditLayerRendererState> GetEditLayerRendererStates(const ULandscapeInfo* InLandscapeInfo, bool bInSkipBrush) override;
	//~ End UE::Landscape::EditLayers::IEditLayerRendererProvider implementation
#endif // WITH_EDITOR

	// TODO [jonathan.bard] : make those private
public:
	UPROPERTY(meta = (IgnoreForMemberInitializationTest))
	FGuid Guid;

	UPROPERTY()
	FName Name;

	UPROPERTY(Transient)
	bool bVisible;

	UPROPERTY()
	bool bLocked;

	UPROPERTY()
	float HeightmapAlpha;

	UPROPERTY()
	float WeightmapAlpha;

	UPROPERTY()
	TEnumAsByte<enum ELandscapeBlendMode> BlendMode;

	UPROPERTY()
	TArray<FLandscapeLayerBrush> Brushes;

	UPROPERTY()
	TMap<TObjectPtr<ULandscapeLayerInfoObject>, bool> WeightmapLayerAllocationBlend; // True -> Substractive, False -> Additive

	UPROPERTY(Instanced)
	TObjectPtr<ULandscapeEditLayerBase> EditLayer;
};

enum class ELandscapeEditLayersMergeMode : uint8
{
	GlobalMerge = 0,
	LocalMerge, 
	BatchedMerge,
	Invalid 
};

// TODO [jonathan.bard] : When global merge is a thing of the past, we should pass FOnRenderBatchTargetGroupDoneParams here, or extract some of its higher-level information, because there
//  is a lot more useful information accessible in there (components being rendered, target layers, validity render targets, etc.)
struct FOnLandscapeEditLayersMergedParams
{
	FOnLandscapeEditLayersMergedParams(UTextureRenderTarget* InRenderTarget, const FIntPoint& InRenderAreaResolution, bool bInIsHeightmapMerge)
		: RenderTarget(InRenderTarget)
		, RenderAreaResolution(InRenderAreaResolution)
		, bIsHeightmapMerge(bInIsHeightmapMerge)
	{}

	/**
	* Render target of the section of landscape that was rendered (important note: the render target's resolution can be larger than the actual landscape resolution, so RenderAreaResolution must be used.
	* In the case of weightmaps, the render target will actually be a UTextureRenderTarget2DArray
	*/
	UTextureRenderTarget* RenderTarget = nullptr;

	/** Actual resolution of this render : render targets are usually sized larger than the effective resolution at this point, so this needs to be used instead.
	 *  Note : it's the true resolution of the landscape : at this point, there are no duplicate vertices in the render target. */
	FIntPoint RenderAreaResolution = FIntPoint(ForceInit);

	bool bIsHeightmapMerge = false;
};

UCLASS(MinimalAPI, showcategories=(Display, Movement, Collision, Lighting, LOD, Input), hidecategories=(Mobility))
class ALandscape : public ALandscapeProxy
#if CPP && WITH_EDITOR // UHT doesn't support inheriting from namespaced class
	, public UE::Landscape::EditLayers::IEditLayerRendererProvider
#endif // CPP && WITH_EDITOR
{
	GENERATED_BODY()

	friend class FLandscapeConfigHelper; // For copying/manipulating internal data

public:
	ALandscape(const FObjectInitializer& ObjectInitializer);

	//~ Begin ALandscapeProxy Interface
	LANDSCAPE_API virtual ALandscape* GetLandscapeActor() override;
	LANDSCAPE_API virtual const ALandscape* GetLandscapeActor() const override;
	//~ End ALandscapeProxy Interface

#if WITH_EDITOR
	static LANDSCAPE_API FName AffectsLandscapeActorDescProperty;

	LANDSCAPE_API bool HasAllComponent(); // determine all component is in this actor
	
	// Include Components with overlapped vertices
	// X2/Y2 Coordinates are "inclusive" max values
	LANDSCAPE_API static void CalcComponentIndicesOverlap(const int32 X1, const int32 Y1, const int32 X2, const int32 Y2, const int32 ComponentSizeQuads, 
		int32& ComponentIndexX1, int32& ComponentIndexY1, int32& ComponentIndexX2, int32& ComponentIndexY2);

	// Exclude Components with overlapped vertices
	// X2/Y2 Coordinates are "inclusive" max values
	LANDSCAPE_API static void CalcComponentIndicesNoOverlap(const int32 X1, const int32 Y1, const int32 X2, const int32 Y2, const int32 ComponentSizeQuads,
		int32& ComponentIndexX1, int32& ComponentIndexY1, int32& ComponentIndexX2, int32& ComponentIndexY2);

	LANDSCAPE_API static void SplitHeightmap(ULandscapeComponent* Comp, ALandscapeProxy* TargetProxy = nullptr, class FMaterialUpdateContext* InOutUpdateContext = nullptr, TArray<class FComponentRecreateRenderStateContext>* InOutRecreateRenderStateContext = nullptr, bool InReregisterComponent = true);
	
	//~ Begin UObject Interface.
	virtual void PreEditChange(FProperty* PropertyThatWillChange) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditMove(bool bFinished) override;
	virtual void PostEditUndo() override;
	virtual void PostRegisterAllComponents() override;
	virtual void PostActorCreated() override;
	virtual bool ShouldImport(FStringView ActorPropString, bool IsMovingLevel) override;
	virtual void PostEditImport() override;
	virtual void PostDuplicate(bool bDuplicateForPIE) override;
	virtual bool CanDeleteSelectedActor(FText& OutReason) const override;
	virtual bool CanChangeIsSpatiallyLoadedFlag() const override { return false; }
#endif // WITH_EDITOR

	virtual void PostInitProperties() override;
	virtual void PostLoad() override;
	virtual void BeginDestroy() override;
	virtual void FinishDestroy() override;
	//~ End UObject Interface

	/** Computes & returns bounds containing all currently loaded landscape proxies (if any) or this landscape's bounds otherwise */
	LANDSCAPE_API FBox GetLoadedBounds() const;

	LANDSCAPE_API bool IsUpToDate() const;
	LANDSCAPE_API void TickLayers(float DeltaTime);

	LANDSCAPE_API void SetLODGroupKey(uint32 InLODGroupKey);
	LANDSCAPE_API uint32 GetLODGroupKey();

	/**
	* Render the final heightmap in the requested top-down window as one -atlased- texture in the provided render target 2D
	*  Can be called at runtime.
	* @param InWorldTransform World transform of the area where the texture should be rendered
	* @param InExtents Extents of the area where the texture should be rendered (local to InWorldTransform). If size is zero, then the entire loaded landscape will be exported.
	* @param OutRenderTarget Render target in which the texture will be rendered. The size/format of the render target will be respected.
	* @return false in case of failure (e.g. invalid inputs, incompatible render target format...)
	*/
	UFUNCTION(BlueprintCallable, Category = "Landscape|Runtime")
	LANDSCAPE_API bool RenderHeightmap(FTransform InWorldTransform, FBox2D InExtents, UTextureRenderTarget2D* OutRenderTarget);

	/**
	* Render the final weightmap for the requested layer, in the requested top-down window, as one -atlased- texture in the provided render target 2D
	*  Can be called at runtime.
	* @param InWorldTransform World transform of the area where the texture should be rendered
	* @param InExtents Extents of the area where the texture should be rendered (local to InWorldTransform). If size is zero, then the entire loaded landscape will be exported.
	* @param InWeightmapLayerName Weightmap layer that is being requested to render
	* @param OutRenderTarget Render target in which the texture will be rendered. The size/format of the render target will be respected.
	* @return false in case of failure (e.g. invalid inputs, incompatible render target format...)
	*/
	UFUNCTION(BlueprintCallable, Category = "Landscape|Runtime")
	LANDSCAPE_API bool RenderWeightmap(FTransform InWorldTransform, FBox2D InExtents, FName InWeightmapLayerName, UTextureRenderTarget2D* OutRenderTarget);

	/**
	* Render the final weightmaps for the requested layers, in the requested top-down window, as one -atlased- texture in the provided render target (2D or 2DArray) 
	*  Can be called at runtime.
	* @param InWorldTransform World transform of the area where the texture should be rendered
	* @param InExtents Extents of the area where the texture should be rendered (local to InWorldTransform). If size is zero, then the entire loaded landscape will be exported.
	* @param InWeightmapLayerNames List of weightmap layers that are being requested to render
	* @param OutRenderTarget Render target in which the texture will be rendered. The size/format of the render target will be respected.
	*  - If a UTextureRenderTarget2D is passed, the requested layers will be packed in the RGBA channels in order (up to the number of channels available with the render target's format).
	*  - If a UTextureRenderTarget2DArray is passed, the requested layers will be packed in the RGBA channels of each slice (up to the number of channels * slices available with the render target's format and number of slices).
	* @return false in case of failure (e.g. invalid inputs, incompatible render target format...)
	*/
	UFUNCTION(BlueprintCallable, Category = "Landscape|Runtime")
	LANDSCAPE_API bool RenderWeightmaps(FTransform InWorldTransform, FBox2D InExtents, const TArray<FName>& InWeightmapLayerNames, UTextureRenderTarget* OutRenderTarget);

	/** 
	* Retrieves the names of valid paint layers on this landscape (editor-only : returns nothing at runtime) 
	* @Param bInIncludeVisibilityLayer whether the visibility layer's name should be included in the list or not
	* @return the list of paint layer names
	*/
	UFUNCTION(BlueprintCallable, BlueprintPure=false, Category = "Landscape|Editor", meta=(DevelopmentOnly))
	TArray<FName> GetTargetLayerNames(bool bInIncludeVisibilityLayer = false) const;

	bool IsValidRenderTargetFormatHeightmap(EPixelFormat InRenderTargetFormat, bool& bOutCompressHeight);
	bool IsValidRenderTargetFormatWeightmap(EPixelFormat InRenderTargetFormat, int32& OutNumChannels);

#if WITH_EDITOR

	/** Computes & returns bounds containing all landscape proxies (if any) or this landscape's bounds otherwise. Note that in non-WP worlds this will call GetLoadedBounds(). */
	LANDSCAPE_API FBox GetCompleteBounds() const;
	void RegisterLandscapeEdMode(ILandscapeEdModeInterface* InLandscapeEdMode) { LandscapeEdMode = InLandscapeEdMode; }
	void UnregisterLandscapeEdMode() { LandscapeEdMode = nullptr; }
	bool HasLandscapeEdMode() const { return LandscapeEdMode != nullptr; }
	LANDSCAPE_API virtual bool HasLayersContent() const override;
	LANDSCAPE_API virtual void UpdateCachedHasLayersContent(bool bInCheckComponentDataIntegrity) override;
	LANDSCAPE_API void RequestSplineLayerUpdate();
	LANDSCAPE_API void RequestLayersInitialization(bool bInRequestContentUpdate = true);
	LANDSCAPE_API void RequestLayersContentUpdateForceAll(ELandscapeLayerUpdateMode InModeMask = ELandscapeLayerUpdateMode::Update_All, bool bInUserTriggered = false);
	LANDSCAPE_API void RequestLayersContentUpdate(ELandscapeLayerUpdateMode InModeMask);
	LANDSCAPE_API bool ReorderLayer(int32 InStartingLayerIndex, int32 InDestinationLayerIndex);
	LANDSCAPE_API FLandscapeLayer* DuplicateLayerAndMoveBrushes(const FLandscapeLayer& InOtherLayer);

	/** 
	* Creates a new edit layer
	* @Param InName is the name of the new edit layer
	* @Param InEditLayerClass is the class of the edit layer to create. Passing null will create a standard layer of type ULandscapeEditLayer
	* @return the index of the newly-created layer
	*/
	LANDSCAPE_API int32 CreateLayer(FName InName = NAME_None, const TSubclassOf<ULandscapeEditLayerBase>& InEditLayerClass = TSubclassOf<ULandscapeEditLayerBase>());

	/** 
	* Creates a new edit layer by copying all the settings from the layer passed in parameter
	* @Param InLayer edit layer to copy from
	* @return the index of the newly-created layer
	*/
	UE_DEPRECATED(5.5, "Use DuplicateLayerAndMoveBrushes")
	LANDSCAPE_API int32 CreateLayerFrom(const FLandscapeLayer& InLayer);
	LANDSCAPE_API void CreateDefaultLayer();

	LANDSCAPE_API void CopyOldDataToDefaultLayer();
	LANDSCAPE_API void CopyOldDataToDefaultLayer(ALandscapeProxy* Proxy);
	LANDSCAPE_API void AddLayersToProxy(ALandscapeProxy* InProxy);
	LANDSCAPE_API FIntPoint ComputeComponentCounts() const;
	LANDSCAPE_API bool IsLayerNameUnique(const FName& InName) const;
	LANDSCAPE_API void SetLayerName(int32 InLayerIndex, const FName& InName);
	LANDSCAPE_API void SetLayerAlpha(int32 InLayerIndex, const float InAlpha, bool bInHeightmap);
	LANDSCAPE_API float GetLayerAlpha(int32 InLayerIndex, bool bInHeightmap) const;
	LANDSCAPE_API float GetClampedLayerAlpha(float InAlpha, bool bInHeightmap) const;
	LANDSCAPE_API void SetLayerVisibility(int32 InLayerIndex, bool bInVisible, bool bInForIntermediateRender = false);
	LANDSCAPE_API void SetLayerLocked(int32 InLayerIndex, bool bLocked);
	LANDSCAPE_API void SetLayerBlendMode(int32 InLayerIndex, ELandscapeBlendMode InBlendMode);
	LANDSCAPE_API uint8 GetLayerCount() const;
	LANDSCAPE_API TArrayView<const FLandscapeLayer> GetLayers() const { return MakeArrayView(LandscapeEditLayers); }
	LANDSCAPE_API const FLandscapeLayer* GetLayerConst(int32 InLayerIndex) const;
	LANDSCAPE_API const FLandscapeLayer* GetLayerConst(const FGuid& InLayerGuid) const;
	LANDSCAPE_API const FLandscapeLayer* GetLayerConst(const FName& InLayerName) const;
	LANDSCAPE_API int32 GetLayerIndex(const FGuid& InLayerGuid) const;
	LANDSCAPE_API int32 GetLayerIndex(FName InLayerName) const;

	UE_DEPRECATED(5.5, "This has moved to private (GetLayerInternal), use the appropriate setters to mutate the edit layer")
	LANDSCAPE_API FLandscapeLayer* GetLayer(int32 InLayerIndex) { return nullptr; }
	UE_DEPRECATED(5.5, "Use GetLayerConst")
	LANDSCAPE_API const FLandscapeLayer* GetLayer(int32 InLayerIndex) const;
	UE_DEPRECATED(5.5, "Use GetLayerConst")
	LANDSCAPE_API const FLandscapeLayer* GetLayer(const FGuid& InLayerGuid) const;
	UE_DEPRECATED(5.5, "Use GetLayerConst")
	LANDSCAPE_API const FLandscapeLayer* GetLayer(const FName& InLayerName) const;

	UE_DEPRECATED(5.5, "Use ForEachLayerConst")
	LANDSCAPE_API void ForEachLayer(TFunctionRef<void(FLandscapeLayer&)> Fn);
	
	/**
	 * Runs the given function on each edit layer, with the possibility of early exit
	 * Most easily used with a lambda as follows:
	 * ForEachLayerConst([](const FLandscapeLayer& InLayer) -> bool
	 * {
	 *     return continueLoop ? true : false;
	 * });
	 */
	LANDSCAPE_API void ForEachLayerConst(TFunctionRef<bool(const FLandscapeLayer&)> Fn);

	LANDSCAPE_API const FLandscapeLayer* FindLayerOfType(const TSubclassOf<ULandscapeEditLayerBase>& InLayerClass) const;
	LANDSCAPE_API TArray<const FLandscapeLayer*> GetLayersOfType(const TSubclassOf<ULandscapeEditLayerBase>& InLayerClass) const;

	LANDSCAPE_API void GetUsedPaintLayers(int32 InLayerIndex, TArray<ULandscapeLayerInfoObject*>& OutUsedLayerInfos) const;
	LANDSCAPE_API void GetUsedPaintLayers(const FGuid& InLayerGuid, TArray<ULandscapeLayerInfoObject*>& OutUsedLayerInfos) const;
	LANDSCAPE_API void ClearPaintLayer(int32 InLayerIndex, ULandscapeLayerInfoObject* InLayerInfo);
	LANDSCAPE_API void ClearPaintLayer(const FGuid& InLayerGuid, ULandscapeLayerInfoObject* InLayerInfo);
	LANDSCAPE_API void ClearLayer(int32 InLayerIndex, TSet<TObjectPtr<ULandscapeComponent>>* InComponents = nullptr, ELandscapeClearMode InClearMode = ELandscapeClearMode::Clear_All);
	LANDSCAPE_API void ClearLayer(const FGuid& InLayerGuid, TSet<TObjectPtr<ULandscapeComponent>>* InComponents = nullptr, ELandscapeClearMode InClearMode = ELandscapeClearMode::Clear_All, bool bMarkPackageDirty = true);
	LANDSCAPE_API bool DeleteLayer(int32 InLayerIndex);
	LANDSCAPE_API void CollapseLayer(int32 InLayerIndex);
	LANDSCAPE_API void DeleteLayers();
	LANDSCAPE_API void SetEditingLayer(const FGuid& InLayerGuid = FGuid());
	LANDSCAPE_API void SetGrassUpdateEnabled(bool bInGrassUpdateEnabled);
	LANDSCAPE_API const FGuid& GetEditingLayer() const;
	LANDSCAPE_API bool IsMaxLayersReached() const;
	LANDSCAPE_API void ShowOnlySelectedLayer(int32 InLayerIndex);
	LANDSCAPE_API void ShowAllLayers();
	LANDSCAPE_API void UpdateLandscapeSplines(const FGuid& InLayerGuid = FGuid(), bool bInUpdateOnlySelected = false, bool bInForceUpdateAllCompoments = false);

	UE_DEPRECATED(5.5, "Use CreateLayer with a ULandscapeEditLayerSplines instead")
	LANDSCAPE_API void SetLandscapeSplinesReservedLayer(int32 InLayerIndex);
	UE_DEPRECATED(5.5, "Use FindLayerOfType(ULandscapeEditLayerSplines::StaticClass()) instead")
	LANDSCAPE_API struct FLandscapeLayer* GetLandscapeSplinesReservedLayer();
	UE_DEPRECATED(5.5, "Use FindLayerOfType(ULandscapeEditLayerSplines::StaticClass()) instead")
	LANDSCAPE_API const struct FLandscapeLayer* GetLandscapeSplinesReservedLayer() const;
	UE_DEPRECATED(5.5, "Use FindLayerOfType(ULandscapeEditLayerSplines::StaticClass()).Guid == GetEditingLayer() instead")
	LANDSCAPE_API bool IsEditingLayerReservedForSplines() const;

	LANDSCAPE_API bool IsLayerBlendSubstractive(int32 InLayerIndex, const TWeakObjectPtr<ULandscapeLayerInfoObject>& InLayerInfoObj) const;
	LANDSCAPE_API void SetLayerSubstractiveBlendStatus(int32 InLayerIndex, bool InStatus, const TWeakObjectPtr<ULandscapeLayerInfoObject>& InLayerInfoObj);
	LANDSCAPE_API void ReplaceLayerSubstractiveBlendStatus(ULandscapeLayerInfoObject* InFromLayerInfo, ULandscapeLayerInfoObject* InToLayerInfo, bool bInShouldDirtyPackage);

	LANDSCAPE_API int32 GetBrushLayer(const ALandscapeBlueprintBrushBase* InBrush) const;
	LANDSCAPE_API void AddBrushToLayer(int32 InLayerIndex, class ALandscapeBlueprintBrushBase* InBrush);
	LANDSCAPE_API void RemoveBrush(class ALandscapeBlueprintBrushBase* InBrush);
	LANDSCAPE_API void RemoveBrushFromLayer(int32 InLayerIndex, class ALandscapeBlueprintBrushBase* InBrush);
	LANDSCAPE_API void RemoveBrushFromLayer(int32 InLayerIndex, int32 InBrushIndex);
	LANDSCAPE_API int32 GetBrushIndexForLayer(int32 InLayerIndex, class ALandscapeBlueprintBrushBase* InBrush);
	LANDSCAPE_API bool ReorderLayerBrush(int32 InLayerIndex, int32 InStartingLayerBrushIndex, int32 InDestinationLayerBrushIndex);
	LANDSCAPE_API class ALandscapeBlueprintBrushBase* GetBrushForLayer(int32 InLayerIndex, int32 BrushIndex) const;
	LANDSCAPE_API TArray<class ALandscapeBlueprintBrushBase*> GetBrushesForLayer(int32 InLayerIndex) const;
	LANDSCAPE_API void OnBlueprintBrushChanged();
	LANDSCAPE_API void OnLayerInfoSplineFalloffModulationChanged(ULandscapeLayerInfoObject* InLayerInfo);
	LANDSCAPE_API void OnPreSave();

	void ReleaseLayersRenderingResource();
	void ClearDirtyData(ULandscapeComponent* InLandscapeComponent);
	
	LANDSCAPE_API void ToggleCanHaveLayersContent();
	LANDSCAPE_API void ForceUpdateLayersContent(bool bIntermediateRender = false);

	UFUNCTION(BlueprintCallable, Category = "Landscape")
	LANDSCAPE_API void ForceLayersFullUpdate();

	LANDSCAPE_API void InitializeLandscapeLayersWeightmapUsage();

	LANDSCAPE_API bool ComputeLandscapeLayerBrushInfo(FTransform& OutLandscapeTransform, FIntPoint& OutLandscapeSize, FIntPoint& OutLandscapeRenderTargetSize);
	void UpdateProxyLayersWeightmapUsage();
	void ValidateProxyLayersWeightmapUsage() const;

	LANDSCAPE_API void SetUseGeneratedLandscapeSplineMeshesActors(bool bInEnabled);
	LANDSCAPE_API bool GetUseGeneratedLandscapeSplineMeshesActors() const;
	LANDSCAPE_API bool PrepareTextureResources(bool bInWaitForStreaming);

	bool GetVisibilityLayerAllocationIndex() const { return 0; }
	
	LANDSCAPE_API virtual void DeleteUnusedLayers() override;

	LANDSCAPE_API void EnableNaniteSkirts(bool bInEnable, float InSkirtDepth, bool bInShouldDirtyPackage);

	/** Set the target precision on nanite vertex position.  Precision is set to approximately (2^-InPrecision) in world units. */
	LANDSCAPE_API void SetNanitePositionPrecision(int32 InPrecision,  bool bInShouldDirtyPackage);

	LANDSCAPE_API void SetDisableRuntimeGrassMapGeneration(bool bInDisableRuntimeGrassMapGeneration);

	LANDSCAPE_API FName GenerateUniqueLayerName(FName InName = NAME_None) const;

private:
	FLandscapeLayer* GetLayerInternal(int32 InLayerIndex);
	void OnLayerCreatedInternal(FLandscapeLayer& Layer);

	bool SupportsEditLayersLocalMerge();
	bool HasNormalCaptureBPBrushLayer();

	ELandscapeEditLayersMergeMode GetEditLayersMergeMode();
	bool CreateLayersRenderingResource(bool bUseNormalCapture);
	void PrepareEditLayersLocalMergeResources();
	void UpdateLayersContent(bool bInWaitForStreaming = false, bool bInSkipMonitorLandscapeEdModeChanges = false, bool bIntermediateRender = false, bool bFlushRender = false);
	void MonitorShaderCompilation();
	void MonitorLandscapeEdModeChanges();
	
	//~ Begin UE::Landscape::EditLayers::IEditLayerRendererProvider implementation
	virtual TArray<UE::Landscape::EditLayers::FEditLayerRendererState> GetEditLayerRendererStates(const ULandscapeInfo* InLandscapeInfo, bool bInSkipBrush) override;
	//~ End UE::Landscape::EditLayers::IEditLayerRendererProvider implementation

	UE::Landscape::EditLayers::FMergeRenderContext PrepareEditLayersMergeRenderContext(const UE::Landscape::EditLayers::FMergeRenderParams& InParams);

	FLandscapeBrushParameters BuildLandscapeBrushParameters(bool bInIsHeightmapMerge, const FIntRect& InRenderAreaExtents, UTextureRenderTarget2D* InRenderTarget, FName InWeightmapLayerName = NAME_None);

	int32 RegenerateLayersHeightmaps(const FUpdateLayersContentContext& InUpdateLayersContentContext);
	int32 PerformLayersHeightmapsLocalMerge(const FUpdateLayersContentContext& InUpdateLayersContentContext, const FEditLayersHeightmapMergeParams& InMergeParams);
	int32 PerformLayersHeightmapsGlobalMerge(const FUpdateLayersContentContext& InUpdateLayersContentContext, const FEditLayersHeightmapMergeParams& InMergeParams);
	int32 PerformLayersHeightmapsBatchedMerge(const FUpdateLayersContentContext& InUpdateLayersContentContext, const FEditLayersHeightmapMergeParams& InMergeParams);
	void ResolveLayersHeightmapTexture(const FTextureToComponentHelper& MapHelper, const TSet<UTexture2D*>& HeightmapsToResolve, bool bIntermediateRender, TArray<FLandscapeEditLayerComponentReadbackResult>& InOutComponentReadbackResults);

	int32 RegenerateLayersWeightmaps(FUpdateLayersContentContext& InUpdateLayersContentContext);
	int32 PerformLayersWeightmapsLocalMerge(FUpdateLayersContentContext& InUpdateLayersContentContext, const FEditLayersWeightmapMergeParams& InMergeParams);
	int32 PerformLayersWeightmapsGlobalMerge(FUpdateLayersContentContext& InUpdateLayersContentContext, const FEditLayersWeightmapMergeParams& InMergeParams);
	int32 PerformLayersWeightmapsBatchedMerge(FUpdateLayersContentContext& InUpdateLayersContentContext, const FEditLayersWeightmapMergeParams& InMergeParams);
	void ResolveLayersWeightmapTexture(const FTextureToComponentHelper& MapHelper, const TSet<UTexture2D*>& WeightmapsToResolve, bool bIntermediateRender, TArray<FLandscapeEditLayerComponentReadbackResult>& InOutComponentReadbackResults);

	bool HasTextureDataChanged(TArrayView<const FColor> InOldData, TArrayView<const FColor> InNewData, bool bInIsWeightmap, uint64 InPreviousHash, uint64& OutNewHash, TOptional<uint8>& OutChangedWeightmapChannelsMasks) const;
	bool ResolveLayersTexture(FTextureToComponentHelper const& MapHelper, FLandscapeEditLayerReadback* InCPUReadBack, UTexture2D* InOutputTexture, bool bIntermediateRender,
		TArray<FLandscapeEditLayerComponentReadbackResult>& InOutComponentReadbackResults, bool bIsWeightmap);

	static bool IsUpdateFlagEnabledForModes(ELandscapeComponentUpdateFlag InFlag, uint32 InUpdateModes);
	void UpdateForChangedHeightmaps(const TArrayView<FLandscapeEditLayerComponentReadbackResult>& InComponentReadbackResults);
	void UpdateForChangedWeightmaps(const TArrayView<FLandscapeEditLayerComponentReadbackResult>& InComponentReadbackResults);
	uint32 UpdateCollisionAndClients(const TArrayView<FLandscapeEditLayerComponentReadbackResult>& Components);
	uint32 UpdateAfterReadbackResolves(const TArrayView<FLandscapeEditLayerComponentReadbackResult>& Components);

	bool PrepareLayersTextureResources(bool bInWaitForStreaming);
	bool PrepareLayersTextureResources(const TArray<FLandscapeLayer>& InLayers, bool bInWaitForStreaming);
	bool PrepareLayersResources(ERHIFeatureLevel::Type InFeatureLevel, bool bInWaitForStreaming);
	void InvalidateRVTForTextures(const TSet<UTexture2D*>& InTextures);
	void PrepareLayersHeightmapsLocalMergeRenderThreadData(const FUpdateLayersContentContext& InUpdateLayersContentContext, const FEditLayersHeightmapMergeParams& InMergeParams, EditLayersHeightmapLocalMerge_RenderThread::FMergeInfo& OutRenderThreadData);
	void PrepareLayersWeightmapsLocalMergeRenderThreadData(const FUpdateLayersContentContext& InUpdateLayersContentContext, const FEditLayersWeightmapMergeParams& InMergeParams, EditLayersWeightmapLocalMerge_RenderThread::FMergeInfo& OutRenderThreadData);

	void UpdateLayersMaterialInstances(const TArray<ULandscapeComponent*>& InLandscapeComponents);

	void PrepareComponentDataToExtractMaterialLayersCS(const TArray<ULandscapeComponent*>& InLandscapeComponents, const FLandscapeLayer& InLayer, int32 InCurrentWeightmapToProcessIndex, const FIntPoint& InLandscapeBase, class FLandscapeTexture2DResource* InOutTextureData,
														  TArray<struct FLandscapeLayerWeightmapExtractMaterialLayersComponentData>& OutComponentData, TMap<ULandscapeLayerInfoObject*, int32>& OutLayerInfoObjects);
	void PrepareComponentDataToPackMaterialLayersCS(int32 InCurrentWeightmapToProcessIndex, const FIntPoint& InLandscapeBase, const TArray<ULandscapeComponent*>& InAllLandscapeComponents, TArray<UTexture2D*>& InOutProcessedWeightmaps,
													TArray<FLandscapeEditLayerReadback*>& OutProcessedCPUReadBacks, TArray<struct FLandscapeLayerWeightmapPackMaterialLayersComponentData>& OutComponentData);
	void ReallocateLayersWeightmaps(FUpdateLayersContentContext& InUpdateLayersContentContext, const TArray<ULandscapeLayerInfoObject*>& InBrushRequiredAllocations, 
		const TMap<ULandscapeComponent*, TArray<ULandscapeLayerInfoObject*>>* InPerComponentAllocations, bool bInRestrictTextureSharingToTheseComponents);
	void InitializeLayersWeightmapResources();
	bool GenerateZeroAllocationPerComponents(const TArray<ALandscapeProxy*>& InAllLandscape, const TMap<ULandscapeLayerInfoObject*, bool>& InWeightmapLayersBlendSubstractive);

	void GenerateLayersRenderQuad(const FIntPoint& InVertexPosition, float InVertexSize, const FVector2D& InUVStart, const FVector2D& InUVSize, TArray<struct FLandscapeLayersTriangle>& OutTriangles) const;
	void GenerateLayersRenderQuadsAtlas(const FIntPoint& InSectionBase, const FVector2D& InScaleBias, float InSubSectionSizeQuad, const FIntPoint& InReadSize, const FIntPoint& InWriteSize, TArray<struct FLandscapeLayersTriangle>& OutTriangles) const;
	void GenerateLayersRenderQuadsAtlasToNonAtlas(const FIntPoint& InSectionBase, const FVector2D& InScaleBias, float InSubSectionSizeQuad, const FIntPoint& InReadSize, const FIntPoint& InWriteSize, TArray<struct FLandscapeLayersTriangle>& OutTriangles) const;
	void GenerateLayersRenderQuadsNonAtlas(const FIntPoint& InSectionBase, const FVector2D& InScaleBias, float InSubSectionSizeQuad, const FIntPoint& InReadSize, const FIntPoint& InWriteSize, TArray<struct FLandscapeLayersTriangle>& OutTriangles) const;
	void GenerateLayersRenderQuadsNonAtlasToAtlas(const FIntPoint& InSectionBase, const FVector2D& InScaleBias, float InSubSectionSizeQuad, const FIntPoint& InReadSize, const FIntPoint& InWriteSize, TArray<struct FLandscapeLayersTriangle>& OutTriangles) const;
	void GenerateLayersRenderQuadsMip(const FIntPoint& InSectionBase, const FVector2D& InScaleBias, float InSubSectionSizeQuad, const FIntPoint& InReadSize, const FIntPoint& InWriteSize, uint8 InCurrentMip, TArray<FLandscapeLayersTriangle>& OutTriangles) const;

	void ClearLayersWeightmapTextureResource(const FString& InDebugName, FTextureRenderTargetResource* InTextureResourceToClear) const;
	void DrawHeightmapComponentsToRenderTarget(const FString& InDebugName, const TArray<ULandscapeComponent*>& InComponentsToDraw, const FIntPoint& InLandscapeBase, UTexture* InHeightmapRTRead, UTextureRenderTarget2D* InOptionalHeightmapRTRead2, UTextureRenderTarget2D* InHeightmapRTWrite, ERTDrawingType InDrawType,
											   bool InClearRTWrite, struct FLandscapeLayersHeightmapShaderParameters& InShaderParams, uint8 InMipRender = 0) const;

	void DrawWeightmapComponentsToRenderTarget(const FString& InDebugName, const TArray<FIntPoint>& InSectionBaseList, const FVector2f& InScaleBias, TArray<FVector2f>* InScaleBiasPerSection, UTexture* InWeightmapRTRead, UTextureRenderTarget2D* InOptionalWeightmapRTRead2, UTextureRenderTarget2D* InWeightmapRTWrite, ERTDrawingType InDrawType,
												bool InClearRTWrite, struct FLandscapeLayersWeightmapShaderParameters& InShaderParams, uint8 InMipRender) const;

	void DrawWeightmapComponentsToRenderTarget(const FString& InDebugName, const TArray<ULandscapeComponent*>& InComponentsToDraw, const FIntPoint& InLandscapeBase, UTexture* InWeightmapRTRead, UTextureRenderTarget2D* InOptionalWeightmapRTRead2, UTextureRenderTarget2D* InWeightmapRTWrite, ERTDrawingType InDrawType,
												bool InClearRTWrite, struct FLandscapeLayersWeightmapShaderParameters& InShaderParams, uint8 InMipRender) const;

	void DrawHeightmapComponentsToRenderTargetMips(const TArray<ULandscapeComponent*>& InComponentsToDraw, const FIntPoint& InLandscapeBase, UTexture* InReadHeightmap, bool InClearRTWrite, struct FLandscapeLayersHeightmapShaderParameters& InShaderParams) const;
	void DrawWeightmapComponentToRenderTargetMips(const TArray<FVector2f>& InTexturePositionsToDraw, UTexture* InReadWeightmap, bool InClearRTWrite, struct FLandscapeLayersWeightmapShaderParameters& InShaderParams) const;

	void CopyTexturePS(const FString& InSourceDebugName, FTextureResource* InSourceResource, const FString& InDestDebugName, FTextureResource* InDestResource) const;

	void InitializeLayers(bool bUseNormalCapture);
	
	void PrintLayersDebugRT(const FString& InContext, UTextureRenderTarget2D* InDebugRT, uint8 InMipRender = 0, bool InOutputHeight = true, bool InOutputNormals = false) const;
	void PrintLayersDebugTextureResource(const FString& InContext, FTextureResource* InTextureResource, uint8 InMipRender = 0, bool InOutputHeight = true, bool InOutputNormals = false) const;
	void PrintLayersDebugHeightData(const FString& InContext, const TArray<FColor>& InHeightmapData, const FIntPoint& InDataSize, uint8 InMipRender, bool InOutputNormals = false) const;
	void PrintLayersDebugWeightData(const FString& InContext, const TArray<FColor>& InWeightmapData, const FIntPoint& InDataSize, uint8 InMipRender) const;

	void UpdateWeightDirtyData(ULandscapeComponent* InLandscapeComponent, UTexture2D const* InWeightmap, FColor const* InOldData, FColor const* InNewData, uint8 InChannel);
	void OnDirtyWeightmap(FTextureToComponentHelper const& MapHelper, UTexture2D const* InWeightmap, FColor const* InOldData, FColor const* InNewData, int32 InMipLevel, uint8 ChangedChannelsMask);
	void UpdateHeightDirtyData(ULandscapeComponent* InLandscapeComponent, UTexture2D const* InHeightmap, FColor const* InOldData, FColor const* InNewData);
	void OnDirtyHeightmap(FTextureToComponentHelper const& MapHelper, UTexture2D const* InWeightmap, FColor const* InOldData, FColor const* InNewData, int32 InMipLevel);

	static bool IsMaterialResourceCompiled(FMaterialResource* InMaterialResource, bool bInWaitForCompilation);
#endif // WITH_EDITOR

private:
	void MarkAllLandscapeRenderStateDirty();
	bool RenderMergedTextureInternal(const FTransform& InRenderAreaWorldTransform, const FBox2D& InRenderAreaExtents, const TArray<FName>& InWeightmapLayerNames, UTextureRenderTarget* OutRenderTarget);

public:

#if WITH_EDITORONLY_DATA
	/** Landscape actor has authority on default streaming behavior for new actors : LandscapeStreamingProxies & LandscapeSplineActors */
	UPROPERTY(EditAnywhere, Category = WorldPartition)
	bool bAreNewLandscapeActorsSpatiallyLoaded = true;

	/** If true, LandscapeStreamingProxy actors have the grid size included in their name, for backward compatibility we also check the AWorldSettings::bIncludeGridSizeInNameForPartitionedActors */
	UPROPERTY()
	bool bIncludeGridSizeInNameForLandscapeActors = false;

	UPROPERTY(EditAnywhere, Category=Landscape)
	bool bCanHaveLayersContent = false;

	/*
	 * If true, WorldPartitionLandscapeSplineMeshesBuilder is responsible of generating partitioned actors of type ALandscapeSplineMeshesActor that will contain all landscape spline/controlpoints static meshes. 
	 * Source components will be editor only and hidden in game for PIE.
	 */
	UPROPERTY()
	bool bUseGeneratedLandscapeSplineMeshesActors = false;

	DECLARE_EVENT(ALandscape, FLandscapeBlueprintBrushChangedDelegate);
	FLandscapeBlueprintBrushChangedDelegate& OnBlueprintBrushChangedDelegate() { return LandscapeBlueprintBrushChangedDelegate; }

	/** Delegate that will be called whenever an edit layers merge is done */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnEditLayersMergedDelegate, const FOnLandscapeEditLayersMergedParams& /*InParams*/);
	FOnEditLayersMergedDelegate::RegistrationType& OnEditLayersMerged() const { return OnEditLayersMergedDelegate; }

	/** Target Landscape Layer for Landscape Splines */
	UE_DEPRECATED(5.5, "This has been refactored into the generic ULandscapeEditLayerBase system. Please check for the presence of a ULandscapeEditLayerSplines layer instead")
	UPROPERTY()
	FGuid LandscapeSplinesTargetLayerGuid_DEPRECATED;
	
	/** Current Editing Landscape Layer*/
	FGuid EditingLayer;

	/** Used to temporarily disable Grass Update in Editor */
	bool bGrassUpdateEnabled;

	UPROPERTY(Transient)
	bool bEnableEditorLayersTick = true;

	UPROPERTY(Transient, DuplicateTransient, TextExportTransient, NonPIEDuplicateTransient)
	bool bWarnedGlobalMergeDimensionsExceeded = false;

	UE_DEPRECATED(5.5, "This property has moved to private. Use the public accessors instead")
	UPROPERTY()
	TArray<FLandscapeLayer> LandscapeLayers_DEPRECATED;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextureRenderTarget2D>> HeightmapRTList;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextureRenderTarget2D>> WeightmapRTList;

	/** List of textures that are not fully streamed in yet (updated every frame to track textures that have finished streaming in) */
	UPROPERTY(Transient, DuplicateTransient, TextExportTransient)	
	TArray<TWeakObjectPtr<UTexture2D>> TrackedStreamingInTextures;

private:
	UPROPERTY()
	TArray<FLandscapeLayer> LandscapeEditLayers;

	FLandscapeBlueprintBrushChangedDelegate LandscapeBlueprintBrushChangedDelegate;
	mutable FOnEditLayersMergedDelegate OnEditLayersMergedDelegate;

	/** Components affected by landscape splines (used to partially clear Layer Reserved for Splines) */
	UPROPERTY(Transient)
	TSet<TObjectPtr<ULandscapeComponent>> LandscapeSplinesAffectedComponents;

	/** Provides information from LandscapeEdMode */
	ILandscapeEdModeInterface* LandscapeEdMode;

	/** Information provided by LandscapeEdMode */
	struct FLandscapeEdModeInfo
	{
		FLandscapeEdModeInfo();

		int32 ViewMode;
		FGuid SelectedLayer;
		TWeakObjectPtr<ULandscapeLayerInfoObject> SelectedLayerInfoObject;
		ELandscapeToolTargetType ToolTarget;
	};

	FLandscapeEdModeInfo LandscapeEdModeInfo;

	UPROPERTY(Transient)
	bool bLandscapeLayersAreInitialized;
	
	UPROPERTY(Transient)
	bool bLandscapeLayersAreInitializedForNormalCapture;

	UPROPERTY(Transient)
	bool bLandscapeLayersAreUsingLocalMerge;

	UPROPERTY(Transient)
	bool WasCompilingShaders;

	UPROPERTY(Transient)
	uint32 LayerContentUpdateModes;
		
	UPROPERTY(Transient)
	bool bSplineLayerUpdateRequested;

	ELandscapeEditLayersMergeMode CurrentEditLayersMergeMode = ELandscapeEditLayersMergeMode::GlobalMerge;

	struct FWaitingForResourcesNotificationHelper
	{
		void Notify(ALandscape* InLandscape, class FLandscapeNotificationManager* InNotificationManager, ELandscapeNotificationType InNotificationType, const FText& InNotificationText);
		void Reset();

		/** Time since waiting for resources to be ready */
		double WaitingForResourcesStartTime = -1.0;

		/** Non-stackable user notification for landscape editor */
		TSharedPtr<FLandscapeNotification> Notification;
	};

	/** Non-stackable user notifications for landscape editor */
	FWaitingForResourcesNotificationHelper WaitingForTexturesNotificationHelper;
	FWaitingForResourcesNotificationHelper WaitingForEditLayerResourcesNotificationHelper;

	TSharedPtr<FLandscapeNotification> InvalidShadingModelNotification;

	// Represent all the resolved paint layer, from all layers blended together (size of the landscape x material layer count)
	class FLandscapeTexture2DArrayResource* CombinedLayersWeightmapAllMaterialLayersResource;
	
	// Represent all the resolved paint layer, from the current layer only (size of the landscape x material layer count)
	class FLandscapeTexture2DArrayResource* CurrentLayersWeightmapAllMaterialLayersResource;	
	
	// Used in extracting the material layers data from layer weightmaps (size of the landscape)
	class FLandscapeTexture2DResource* WeightmapScratchExtractLayerTextureResource;	
	
	// Used in packing the material layer data contained into CombinedLayersWeightmapAllMaterialLayersResource to be set again for each component weightmap (size of the landscape)
	class FLandscapeTexture2DResource* WeightmapScratchPackLayerTextureResource;
#endif
};

#if WITH_EDITOR
class FScopedSetLandscapeEditingLayer
{
public:
	LANDSCAPE_API FScopedSetLandscapeEditingLayer(ALandscape* InLandscape, const FGuid& InLayerGUID, TFunction<void()> InCompletionCallback = TFunction<void()>());
	LANDSCAPE_API ~FScopedSetLandscapeEditingLayer();

private:
	TWeakObjectPtr<ALandscape> Landscape;
	FGuid PreviousLayerGUID;
	TFunction<void()> CompletionCallback;
};
#endif
