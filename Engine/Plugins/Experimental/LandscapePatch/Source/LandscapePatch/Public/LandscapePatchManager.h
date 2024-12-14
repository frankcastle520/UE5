// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


#include "LandscapeBlueprintBrushBase.h"
#include "LandscapeEditTypes.h"
#include "LandscapePatchEditLayer.h" // PATCH_PRIORITY_BASE

#include "LandscapePatchManager.generated.h"

class ALandscape;
class ULandscapePatchComponent;

/**
 * Actor used in legacy landscape patch handling where a manager keeps a serialized list
 * of patches that determines their priority. This approach is deprecated- patches now
 * point to a special landscape patch edit layer via a guid, and determine their ordering
 * relative to each other using a priority value.
 */
UCLASS()
class ALandscapePatchManager : public ALandscapeBlueprintBrushBase
{
	GENERATED_BODY()

public:
	ALandscapePatchManager(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	// These get called in the global merge path.
	virtual void Initialize_Native(const FTransform& InLandscapeTransform,
		const FIntPoint& InLandscapeSize,
		const FIntPoint& InLandscapeRenderTargetSize) override;
	virtual UTextureRenderTarget2D* RenderLayer_Native(const FLandscapeBrushParameters& InParameters) override;

#if WITH_EDITOR
	using FEditLayerRendererState = UE::Landscape::EditLayers::FEditLayerRendererState;

	// IEditLayerRendererProvider
	// Called by the batched-merge application path.
	virtual TArray<FEditLayerRendererState> GetEditLayerRendererStates(const ULandscapeInfo* InLandscapeInfo, bool bInSkipBrush) override;

	// ILandscapeEditLayerRenderer
	//~ In batched merge, the manager relies on being a renderer provider. It does not need to have its 
	//~  RenderLayer_Native method called, so we override the implementations of ILandscapeEditLayerRenderer
	//~  inherited from ALandscapeBlueprintBrushBase to do nothing.
	virtual void GetRendererStateInfo(const ULandscapeInfo* InLandscapeInfo,
		UE::Landscape::EditLayers::FEditLayerTargetTypeState& OutSupportedTargetTypeState,
		UE::Landscape::EditLayers::FEditLayerTargetTypeState& OutEnabledTargetTypeState,
		TArray<TSet<FName>>& OutRenderGroups) const override {}
	virtual TArray<UE::Landscape::EditLayers::FEditLayerRenderItem> GetRenderItems(const ULandscapeInfo* InLandscapeInfo) const override
	{
		return {};
	}
	virtual void RenderLayer(ILandscapeEditLayerRenderer::FRenderParams& InRenderParams) override { };
#endif // WITH_EDITOR

	// Adds the brush to the given landscape, removing it from any previous one. This differs from SetOwningLandscape
	// in that SetOwningLandscape is called by the landscape itself from AddBrushToLayer to update the manager.
	UFUNCTION(BlueprintCallable, Category = LandscapeManager)
	virtual void SetTargetLandscape(ALandscape* InOwningLandscape);

	// For use by the owned patch objects.
	/**
	 * Gets the transform from a point in the heightmap (where x and y are pixel coordinates,
	 * aka coordinates of the associated vertex, and z is the height as stored in the height
	 * map, currently a 16 bit integer) to world point based on the current landscape transform.
	 */
	virtual FTransform GetHeightmapCoordsToWorld() { return HeightmapCoordsToWorld; }

	UFUNCTION(BlueprintCallable, Category = LandscapePatch)
	bool ContainsPatch(ULandscapePatchComponent* Patch) const;

	UFUNCTION(BlueprintCallable, Category = LandscapePatch)
	void AddPatch(ULandscapePatchComponent* Patch);

	UFUNCTION(BlueprintCallable, Category = LandscapePatch)
	bool RemovePatch(ULandscapePatchComponent* Patch);

	/** 
	 * Gets the index of a particular patch in the manager's stack of patches (later indices get applied after
	 * earlier ones.
	 */
	UFUNCTION(BlueprintCallable, Category = LandscapePatch)
	int32 GetIndexOfPatch(const ULandscapePatchComponent* Patch) const;

	/**
	 * Moves patch to given index in the list of patches held by the manager (so that it is applied at 
	 * a particular time relative to the others).
	 */
	UFUNCTION(BlueprintCallable, Category = LandscapePatch)
	void MovePatchToIndex(ULandscapePatchComponent* Patch, int32 Index);

#if WITH_EDITOR

	/**
	 * Move any patches from legacy patch list to being bound directly to an edit layer,
	 * and delete the patch manager. This will cause a popup to the user if there is still
	 * a dangling reference to the manager (there shouldn't be).
	 */
	UFUNCTION(CallInEditor, Category = LandscapeManager, meta = (DisplayName = "MigrateToPrioritySystem"))
	void MigrateToPrioritySystemAndDelete();

	// ALandscapeBlueprintBrushBase
	virtual bool CanAffectWeightmapLayer(const FName& InLayerName) const override;
	virtual bool AffectsHeightmap() const override;
	virtual bool AffectsWeightmap() const override;
	virtual bool AffectsWeightmapLayer(const FName& InLayerName) const override;
	virtual bool AffectsVisibilityLayer() const override;
	virtual void GetRenderDependencies(TSet<UObject*>& OutDependencies) override;
	virtual void SetOwningLandscape(class ALandscape* InOwningLandscape) override;

	// AActor
	virtual void CheckForErrors() override;

	// UObject
	virtual void PostEditUndo() override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostLoad() override;
#endif
	virtual bool IsEditorOnly() const override { return true; }
	virtual bool NeedsLoadForClient() const override { return false; }
	virtual bool NeedsLoadForServer() const override { return false; }

	// This is intentionally lower than PATCH_PRIORITY_BASE so that patches converted from a
	// patch manager list are applied before other edit layer patches.
	inline static const double LEGACY_PATCH_PRIORITY_BASE = ULandscapePatchEditLayer::PATCH_PRIORITY_BASE - 10;
private:

	UPROPERTY()
	TArray<TSoftObjectPtr<ULandscapePatchComponent>> PatchComponents;

	// Used in legacy paths to pass the transform information from Initialize_Native to RenderLayer_Native
	UPROPERTY()
	FTransform HeightmapCoordsToWorld;

#if WITH_EDITORONLY_DATA
	//~ This is transient because SetOwningLandscape is called in ALandscape::PostLoad.
	/**
	 * The owning landscape.
	 */
	UPROPERTY(EditAnywhere, Category = Landscape, Transient, meta = (DisplayName = "Landscape"))
	TObjectPtr<ALandscape> DetailPanelLandscape = nullptr;

	bool bIssuedPatchOwnershipWarning = false;
#endif

	// Transient table to speed up Contains and IndexOf queries, which are very slow for an array of TSoftObjectPtr's.
	UPROPERTY(Transient)
	TMap<TSoftObjectPtr<ULandscapePatchComponent>, int32> PatchToIndex;
};

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_2
#include "CoreMinimal.h"
#endif
