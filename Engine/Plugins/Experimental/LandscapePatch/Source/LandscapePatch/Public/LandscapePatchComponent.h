// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


#include "Components/SceneComponent.h"
#include "LandscapeBlueprintBrushBase.h"
#include "LandscapeEditLayerRenderer.h"
#include "LandscapeEditTypes.h"
#include "LandscapePatchEditLayer.h" // PATCH_PRIORITY_BASE
#include "UObject/WeakInterfacePtr.h"

#include "LandscapePatchComponent.generated.h"

enum class ECacheApplyPhase;
enum class ETeleportType : uint8;

class ALandscape;
class ALandscapePatchManager;
struct FLandscapeLayer;
class UTextureRenderTarget2D;

UENUM()
enum class ELandscapePatchPriorityInitialization : uint8
{
	// Initialize priority to highest currently known value, so that the new patch is on top of any existing
	// patches. Note that the highest known priority could be out of date in between landscape updates if
	// priorities change, so it is possible that adjustment will still be needed.
	AcquireHighest,

	// Do not change the default/archetype priority value. This is useful when using custom priority
	// values as categories.
	KeepOriginal,

	// Increment the original priority by a small amount (0.01). This can be useful when copying a patch
	// around multiple times, as it allows the new patches to be roughly in the same place in the 
	// priority hierarchy while still being higher priority than the copied patch.
	SmallIncrement,
};

/**
 * Base class for landscape patches: components that can be attached to meshes and moved around to make
 * the meshes affect the landscape around themselves.
 */
//~ TODO: Although this doesn't generate geometry, we are likely to change this to inherit from UPrimitiveComponent
//~ so that we can use render proxies for passing along data to the render thread or perhaps for visualization.
UCLASS(Blueprintable, BlueprintType, Abstract)
class LANDSCAPEPATCH_API ULandscapePatchComponent : public USceneComponent
	, public ILandscapeEditLayerRenderer
{
	GENERATED_BODY()

public:
	ULandscapePatchComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	// Called from global merge path
	virtual UTextureRenderTarget2D* RenderLayer_Native(const FLandscapeBrushParameters& InParameters, 
		const FTransform& HeightmapToWorld) { return InParameters.CombinedResult; }

#if WITH_EDITOR
	// ILandscapeEditLayerRenderer
	virtual bool CanRender() const override { return CanAffectLandscape(); }
	// Subclasses are expected to implement GetEditLayerRendererDebugName, GetRendererStateInfo, GetRenderItems, and RenderLayer
#endif

	// These determine whether the patch is configured correctly to affect height/weightmaps,
	// ignoring whether it is currently enabled or not.
	virtual bool CanAffectHeightmap() const { return false; }
	virtual bool CanAffectWeightmap() const { return false; }
	virtual bool CanAffectWeightmapLayer(const FName& InLayerName) const { return false; }
	virtual bool CanAffectVisibilityLayer() const { return false; }

	bool CanAffectLandscape() const { return CanAffectHeightmap() || CanAffectWeightmap() || CanAffectVisibilityLayer(); }

	// These compose IsEnabled with the appropriate CanAffect functions
	bool AffectsHeightmap() const { return IsEnabled() && CanAffectHeightmap(); }
	bool AffectsWeightmap() const { return IsEnabled() && CanAffectWeightmap(); }
	bool AffectsWeightmapLayer(const FName& InLayerName) const { return IsEnabled() && CanAffectWeightmapLayer(InLayerName); }
	bool AffectsVisibilityLayer() const { return IsEnabled() && CanAffectVisibilityLayer(); }

	// Allows the patch to specify textures that need to be ready/compiled before applying the patch
	virtual void GetRenderDependencies(TSet<UObject*>& OutDependencies) const {}

	UFUNCTION(BlueprintCallable, Category = "LandscapePatch")
	void RequestLandscapeUpdate(bool bInUserTriggeredUpdate = false);

	/**
	 * Allows the patch to be disabled, so that it no longer affects the landscape. This can be useful
	 * when deleting the patch is undesirable, usually when the disabling is temporary.
	 */
	UFUNCTION(BlueprintCallable, Category = "LandscapePatch")
	void SetIsEnabled(bool bEnabledIn);

	/**
	 * @return false if the patch is marked as disabled and therefore can't affect the landscape.
	 */
	UFUNCTION(BlueprintCallable, Category = "LandscapePatch")
	virtual bool IsEnabled() const { return bIsEnabled; }

	UFUNCTION(BlueprintCallable, Category = "LandscapePatch")
	double GetPriority() const { return Priority; }

	UFUNCTION(BlueprintCallable, Category = "LandscapePatch")
	void SetPriority(double PriorityIn);

	FGuid GetEditLayerGuid() const { return EditLayerGuid; }
	void SetEditLayerGuid(const FGuid& GuidIn);

	/**
	 * Returns the result of GetTypeHash(GetFullName()) for this component. Useful for quick deterministic
	 * (though not lexical) ordering of same-priority patches. 
	 */
	uint32 GetFullNameHash() const;

	UFUNCTION(BlueprintCallable, Category = "LandscapePatch")
	FTransform GetLandscapeHeightmapCoordsToWorld() const;

	UFUNCTION(BlueprintCallable, Category = "LandscapePatch")
	virtual void SetLandscape(ALandscape* NewLandscape);

	UE_DEPRECATED(5.5, "Patch manager use is deprecated. Patches should point to an edit layer using a Guid, and order themselves using priority.")
	UFUNCTION(BlueprintCallable, Category = "LandscapePatch")
	virtual void SetPatchManager(ALandscapePatchManager* NewPatchManager);

	UE_DEPRECATED(5.5, "Patch manager use is deprecated. Patches should point to an edit layer using a Guid, and order themselves using priority.")
	UFUNCTION(BlueprintCallable, Category = "LandscapePatch")
	virtual ALandscapePatchManager* GetPatchManager() const;

	// Verifies that we're not an archetype or trash or in an invalid world
	bool IsPatchInWorld() const;

	// For now we keep the patches largely editor-only, since we don't yet support runtime landscape editing.
	// The above functions are also editor-only (and don't work at runtime), but can't be in WITH_EDITOR blocks
	// so that they can be called from non-editor-only classes in editor contexts.
#if WITH_EDITOR

	// Cleanup method that can be used to fix the patch's binding to a landscape. An arbitrary landscape
	// will be bound to if no guid, patch manager, or landscape is set on the patch.
	void FixBindings();

	// Allows the patch to update its UI and cached pointer
	void NotifyOfBoundLayerDeletion(ULandscapePatchEditLayer* LayerIn);

	// USceneComponent
	virtual void OnUpdateTransform(EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport) override;

	// UActorComponent
	virtual void CheckForErrors() override;
	virtual void OnComponentCreated() override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
	virtual void OnRegister() override;
	virtual void GetActorDescProperties(FPropertyPairsMap& PropertyPairsMap) const override;
	virtual TStructOnScope<FActorComponentInstanceData> GetComponentInstanceData() const override;

	// UObject
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual bool IsPostLoadThreadSafe() const override { return true; }
	virtual void PostLoad() override;
	virtual void PostEditUndo() override;
#endif
	virtual bool IsEditorOnly() const override { return true; }
	virtual bool NeedsLoadForClient() const override { return false; }
	virtual bool NeedsLoadForServer() const override { return false; }
protected:

#if WITH_EDITOR
	ULandscapePatchEditLayer* GetBoundEditLayer() { return EditLayer.Get(); }
#endif

	// Guid of the edit layer to which the patch is bound, if not using legacy patch manager list.
	UPROPERTY()
	FGuid EditLayerGuid;

	/** How to initialize the patch priority when a patch is first created. */
	UPROPERTY(EditAnywhere, Category = Settings, AdvancedDisplay)
	ELandscapePatchPriorityInitialization PriorityInitialization = ELandscapePatchPriorityInitialization::AcquireHighest;

	// Value that determines the patch ordering relative to other patches. Note that this is
	// disregarded when using the legacy patch manager list (i.e. Patch Manager should be null).
	UPROPERTY(EditAnywhere, Category = Settings)
	double Priority = ULandscapePatchEditLayer::PATCH_PRIORITY_BASE;

	UPROPERTY(EditAnywhere, Category = Settings)
	TSoftObjectPtr<ALandscape> Landscape = nullptr;

	/**
	 * Deprecated: Patch manager that can hold the patch as part of its legacy patch list.
	 * 
	 * Instead, patches should now point to an edit layer via a guid and order themselves using their
	 * priority value.
	 */
	//~ TODO: Deprecate this property, maybe if we have automatic conversion someday?
	UPROPERTY(EditAnywhere, Category = Settings, AdvancedDisplay)
	TSoftObjectPtr<ALandscapePatchManager> PatchManager = nullptr;

	/**
	 * When false, patch does not affect the landscape. Useful for temporarily disabling the patch.
	 */
	UPROPERTY(EditAnywhere, Category = Settings)
	bool bIsEnabled = true;

	// Determines whether the height patch was made by copying a different height patch.
	bool bWasCopy = false;

#if WITH_EDITORONLY_DATA
private:
	// Starts as false and gets set to true in construction, so gets used to set bWasCopy
	// by checking the indicator value at the start of construction.
	UPROPERTY()
	bool bPropertiesCopiedIndicator = false;

	// Used to properly transition to a different manager when editing it via the detail panel.
	UPROPERTY()
	TSoftObjectPtr<ALandscapePatchManager> PreviousPatchManager = nullptr;

	//~ Name of edit layer displayed to user
	/**
	 * Name of the edit layer to which the patch is bound. Options are determined by the set Landscape pointer.
	 */ 
	UPROPERTY(Transient, EditAnywhere, Category = Settings, Meta = (DisplayName="Edit Layer", GetOptions = "GetLayerOptions"))
	FString DetailPanelLayerName = TEXT("-Null-");

	UPROPERTY(Transient, VisibleAnywhere, Category = Settings, AdvancedDisplay, Meta = (DisplayName = "Layer Guid"))
	FString DetailPanelLayerGuid = EditLayerGuid.ToString();
#endif

	//~ Gives a drop down for edit layer names depending on the set landscape. Can't be editor only to be used in GetOptions
	UFUNCTION()
	const TArray<FString> GetLayerOptions();

#if WITH_EDITOR
	// Transient pointer to the edit layer that we are bound to using EditLayerGuid. Will
	// be null if that layer is not of an appropriate type.
	TWeakObjectPtr<ULandscapePatchEditLayer> EditLayer;

	bool BindToEditLayer(const FGuid& Guid);
	void ResetEditLayer();
	bool BindToLandscape(ALandscape* LandscapeIn);
	bool BindToAnyLandscape();
	void UpdateEditLayerFromDetailPanelLayerName();

	void ResetPatchManager();

	// Tells us whether the patch is attached to an actor that is being dragged around as an editor preview
	bool IsPatchPreview();

	// Used to avoid spamming warning messages
	bool bGaveCouldNotBindToEditLayerWarning = false;
	bool bGaveMissingEditLayerGuidWarning = false;
	bool bGaveMismatchedLandscapeWarning = false;
	bool bGaveNotInPatchManagerWarning = false;
	bool bGaveMissingLandscapeWarning = false;
	void ResetWarnings();
	bool bInstanceDataApplied = false;
	bool bDeferUpdateRequestUntilInstanceData = false;
	
	void ApplyComponentInstanceData(struct FLandscapePatchComponentInstanceData* ComponentInstanceData, ECacheApplyPhase CacheApplyPhase);
#endif

	friend struct FLandscapePatchComponentInstanceData;
};

/** Used to store some extra data during RerunConstructionScripts. */
USTRUCT()
struct FLandscapePatchComponentInstanceData : public FSceneComponentInstanceData
{
	GENERATED_BODY()

	FLandscapePatchComponentInstanceData() = default;
	FLandscapePatchComponentInstanceData(const ULandscapePatchComponent* SourceComponent);

	virtual ~FLandscapePatchComponentInstanceData() = default;

	virtual bool ContainsData() const override
	{
		return true;
	}

	virtual void ApplyToComponent(UActorComponent* Component, const ECacheApplyPhase CacheApplyPhase) override
	{
#if WITH_EDITOR
		Super::ApplyToComponent(Component, CacheApplyPhase);
		CastChecked<ULandscapePatchComponent>(Component)->ApplyComponentInstanceData(this, CacheApplyPhase);
#endif
	}

#if WITH_EDITORONLY_DATA
	// The UPROPERTY tags inside a FSceneComponentInstanceData might not be necessary, but might 
	// potentially be used in some multiuser code paths.

	UPROPERTY()
	TSoftObjectPtr<ALandscapePatchManager> PatchManager = nullptr;
	UPROPERTY()
	FGuid EditLayerGuid;
	// Priority needs to be carried over becuse our tweaks to it in OnComponentCreated cause it to not be
	//  captured automatically, as it is detected as "UCS altered".
	UPROPERTY()
	double Priority = 0;

	// Used so that we don't spam warning messages while rerunning construction scripts on a patch
	// that triggers one of the warnings.
	UPROPERTY()
	bool bGaveCouldNotBindToEditLayerWarning = false;
	UPROPERTY()
	bool bGaveMismatchedLandscapeWarning = false;
	UPROPERTY()
	bool bGaveMissingEditLayerGuidWarning = false;
	UPROPERTY()
	bool bGaveNotInPatchManagerWarning = false;
	UPROPERTY()
	bool bGaveMissingLandscapeWarning = false;
#endif
};

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_2
#include "CoreMinimal.h"
#endif
