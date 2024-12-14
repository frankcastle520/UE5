// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CEClonerEffectorShared.h"
#include "CEMeshBuilder.h"
#include "CEPropertyChangeDispatcher.h"
#include "Containers/Ticker.h"
#include "Layouts/CEClonerLayoutBase.h"
#include "NiagaraComponent.h"
#include "CEClonerComponent.generated.h"

class UCEClonerExtensionBase;
class UCEClonerLayoutBase;
class UMaterialInterface;
struct FNiagaraMeshMaterialOverride;

UCLASS(MinimalAPI
	, BlueprintType
	, DisplayName = "Motion Design Cloner Component"
	, AutoExpandCategories=(Cloner, Layout)
	, HideCategories=(Niagara, Activation, Lighting, Attachment, Randomness, Parameters, Warmup, Compilation, Navigation, Tags, LOD, TextureStreaming, Mobile, RayTracing, AssetUserData, Cooking, HLOD, Rendering))
class UCEClonerComponent : public UNiagaraComponent
{
	GENERATED_BODY()

	friend class ACEClonerActor;

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnClonerMeshUpdated, UCEClonerComponent* /** ClonerComponent */)
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnClonerLayoutLoaded, UCEClonerComponent* /** ClonerComponent */, UCEClonerLayoutBase* /** InLayout */)
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnClonerInitialized, UCEClonerComponent* /** ClonerComponent */)

public:
	/** Only materials transient or part of the content folder can be dirtied, engine or plugins cannot */
	static bool IsMaterialDirtyable(const UMaterialInterface* InMaterial);

	/** Check if material has niagara usage flag set */
	static bool IsMaterialUsageFlagSet(const UMaterialInterface* InMaterial);

#if WITH_EDITOR
	/** Show material warning notification when missing niagara usage flag */
	static void ShowMaterialWarning(int32 InMaterialCount);

	static CLONEREFFECTOR_API FName GetActiveExtensionsPropertyName();

	static CLONEREFFECTOR_API FName GetActiveLayoutPropertyName();

	static CLONEREFFECTOR_API FName GetLayoutNamePropertyName();
#endif

	static FOnClonerMeshUpdated::RegistrationType& OnClonerMeshUpdated()
	{
		return OnClonerMeshUpdatedDelegate;
	}

	static FOnClonerLayoutLoaded::RegistrationType& OnClonerLayoutLoaded()
	{
		return OnClonerLayoutLoadedDelegate;
	}

	static FOnClonerInitialized::RegistrationType& OnClonerInitialized()
	{
		return OnClonerInitializedDelegate;
	}

	UCEClonerComponent();

	UFUNCTION(BlueprintCallable, Category="Cloner")
	CLONEREFFECTOR_API void SetEnabled(bool bInEnable);

	UFUNCTION(BlueprintPure, Category="Cloner")
	bool GetEnabled() const
	{
		return bEnabled;
	}

	UFUNCTION(BlueprintCallable, Category="Cloner")
	CLONEREFFECTOR_API void SetTreeUpdateInterval(float InInterval);

	UFUNCTION(BlueprintPure, Category="Cloner")
	float GetTreeUpdateInterval() const
	{
		return TreeUpdateInterval;
	}

	UFUNCTION(BlueprintCallable, Category="Cloner")
	CLONEREFFECTOR_API void SetSeed(int32 InSeed);

	UFUNCTION(BlueprintPure, Category="Cloner")
	int32 GetSeed() const
	{
		return Seed;
	}

	UFUNCTION(BlueprintCallable, Category="Cloner")
	CLONEREFFECTOR_API void SetColor(const FLinearColor& InColor);

	UFUNCTION(BlueprintPure, Category="Cloner")
	const FLinearColor& GetColor() const
	{
		return Color;
	}

	UFUNCTION(BlueprintCallable, Category="Cloner")
	CLONEREFFECTOR_API void SetLayoutName(FName InLayoutName);

	UFUNCTION(BlueprintPure, Category="Cloner")
	FName GetLayoutName() const
	{
		return LayoutName;
	}

	UFUNCTION(BlueprintCallable, Category="Cloner")
	void SetLayoutClass(TSubclassOf<UCEClonerLayoutBase> InLayoutClass);

	UFUNCTION(BlueprintPure, Category="Cloner")
	TSubclassOf<UCEClonerLayoutBase> GetLayoutClass() const;

	UFUNCTION(BlueprintPure, Category="Cloner")
	UCEClonerLayoutBase* GetActiveLayout() const
	{
		return ActiveLayout;
	}

	template<
		typename InLayoutClass
		UE_REQUIRES(TIsDerivedFrom<InLayoutClass, UCEClonerLayoutBase>::Value)>
	bool IsActiveLayout() const
	{
		if (const UCEClonerLayoutBase* CurrentLayout = GetActiveLayout())
		{
			return CurrentLayout->GetClass() == InLayoutClass::StaticClass();
		}

		return false;
	}

	template<
		typename InLayoutClass
		UE_REQUIRES(TIsDerivedFrom<InLayoutClass, UCEClonerLayoutBase>::Value)>
	InLayoutClass* GetActiveLayout() const
	{
		return Cast<InLayoutClass>(GetActiveLayout());
	}

#if WITH_EDITOR
	UFUNCTION(BlueprintCallable, Category="Cloner")
	CLONEREFFECTOR_API void SetVisualizerSpriteVisible(bool bInVisible);

	UFUNCTION(BlueprintPure, Category="Cloner")
	bool GetVisualizerSpriteVisible() const
	{
		return bVisualizerSpriteVisible;
	}
#endif

	/** Returns the number of meshes this cloner currently handles */
	UFUNCTION(BlueprintPure, Category="Cloner")
	CLONEREFFECTOR_API int32 GetMeshCount() const;

	/** Returns the number of root attachment currently on this cloner */
	UFUNCTION(BlueprintPure, Category="Cloner")
	int32 GetAttachmentCount() const;

#if WITH_EDITOR
	/** This will force an update of the cloner attachment tree */
	UFUNCTION(CallInEditor, Category="Cloner")
	void ForceUpdateCloner();

	/** Open project settings for cloner */
	UFUNCTION(CallInEditor, Category="Utilities")
	void OpenClonerSettings();

	/** This will create a new default actor attached to this cloner if nothing is attached to this cloner */
	UFUNCTION(CallInEditor, Category="Utilities")
	void CreateDefaultActorAttached();

	/** Converts the cloner simulation into a single static mesh, this is a heavy operation */
	UFUNCTION(CallInEditor, Category="Utilities")
	void ConvertToStaticMesh();

	/** Converts the cloner simulation into a single dynamic mesh, this is a heavy operation */
	UFUNCTION(CallInEditor, Category="Utilities")
	void ConvertToDynamicMesh();

	/** Converts the cloner simulation into static meshes, this is a heavy operation */
	UFUNCTION(CallInEditor, Category="Utilities")
	void ConvertToStaticMeshes();

	/** Converts the cloner simulation into dynamic meshes, this is a heavy operation */
	UFUNCTION(CallInEditor, Category="Utilities")
	void ConvertToDynamicMeshes();

	/** Converts the cloner simulation into instanced static meshes, this is a heavy operation */
	UFUNCTION(CallInEditor, Category="Utilities")
	void ConvertToInstancedStaticMeshes();
#endif

	/** Will force a system update to refresh user parameters */
	void RequestClonerUpdate(bool bInImmediate = false);

	/** Forces a refresh of the meshes used */
	void RefreshClonerMeshes();

	template<
		typename InExtensionClass>
	InExtensionClass* GetExtension() const
	{
		return Cast<InExtensionClass>(GetExtension(InExtensionClass::StaticClass()));
	}

	UFUNCTION(BlueprintCallable, Category="Cloner")
	UCEClonerExtensionBase* GetExtension(TSubclassOf<UCEClonerExtensionBase> InExtensionClass) const;

	UCEClonerExtensionBase* GetExtension(FName InExtensionName) const;

	TConstArrayView<TObjectPtr<UCEClonerExtensionBase>> GetActiveExtensions() const
	{
		return ActiveExtensions;
	}

	/**
	 * Retrieves all active extensions on this cloner
	 * @param OutExtensions [Out] Active extensions
	 */
	UFUNCTION(BlueprintCallable, Category="Cloner")
	void GetActiveExtensions(TArray<UCEClonerExtensionBase*>& OutExtensions) const
	{
		OutExtensions = ActiveExtensions;
	}

protected:
	/** Called when meshes have been updated */
	CLONEREFFECTOR_API static FOnClonerMeshUpdated OnClonerMeshUpdatedDelegate;

	/** Called when new cloner layout is loaded */
	CLONEREFFECTOR_API static FOnClonerLayoutLoaded OnClonerLayoutLoadedDelegate;

	/** Called when cloner is initialized */
	CLONEREFFECTOR_API static FOnClonerInitialized OnClonerInitializedDelegate;

	/** Replaces all unsupported material by default material, gathers unset materials that needs recompiling with proper flags */
	static bool FilterSupportedMaterials(TArray<TWeakObjectPtr<UMaterialInterface>>& InMaterials, TArray<TWeakObjectPtr<UMaterialInterface>>& OutUnsetMaterials, UMaterialInterface* InDefaultMaterial);

	static bool FilterSupportedMaterial(UMaterialInterface*& InMaterial, UMaterialInterface* InDefaultMaterial);

	/** Fires a warning about unset materials used within this cloner */
	void FireMaterialWarning(const AActor* InContextActor, const TArray<TWeakObjectPtr<UMaterialInterface>>& InUnsetMaterials);

	//~ Begin UObject
	virtual void PostInitProperties() override;
	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PostEditImport() override;
	virtual void PostDuplicate(bool bInPIE) override;
	virtual void PostEditUndo() override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& InPropertyChangedEvent) override;
#endif
	//~ End UObject

	//~ Begin UActorComponent
	virtual void OnComponentCreated() override;
	virtual void OnComponentDestroyed(bool bInDestroyingHierarchy) override;
	//~ End UActorComponent

	void UpdateAttachmentTree();
	void UpdateActorAttachment(AActor* InActor, AActor* InParent);

	/** Get cloner direct children in the correct order */
	void GetOrderedRootActors(TArray<AActor*>& OutActors) const;

	/** Get the root cloner actor (direct child) for a specific actor */
	AActor* GetRootActor(AActor* InActor) const;

	/** Resets the root baked static mesh to be regenerated later */
	void InvalidateBakedStaticMesh(AActor* InActor);

	/** Runs async update to rebuild dirty meshes */
	void UpdateDirtyMeshesAsync();
	void OnDirtyMeshesUpdated(bool bInSuccess);

	/** Merges all primitive components from an actor to dynamic mesh, does not recurse */
	void UpdateActorBakedDynamicMesh(AActor* InActor);

	/** Merges all baked dynamic meshes from children and self into one static mesh to use as niagara mesh input */
	void UpdateRootActorBakedStaticMesh(AActor* InRootActor);

	/** Gets all attachment items based on an actor, will recurse */
	void GetActorAttachmentItems(AActor* InActor, TArray<FCEClonerAttachmentItem*>& OutAttachmentItems);

	/** Update niagara asset static meshes */
	void UpdateClonerMeshes();

	/** Checks that all root static meshes are valid */
	bool IsAllMergedMeshesValid() const;

	/** Sets the layout to use for this cloner simulation */
	void SetClonerActiveLayout(UCEClonerLayoutBase* InLayout);

	void OnActiveLayoutLoaded(UCEClonerLayoutBase* InLayout, bool bInSuccess);

	void ActivateLayout(UCEClonerLayoutBase* InLayout);

	void OnActiveLayoutChanged();

	/** Is this cloner enabled/disabled */
	UPROPERTY(EditInstanceOnly, Setter="SetEnabled", Getter="GetEnabled", Category="Cloner")
	bool bEnabled = true;

	/** Interval to update the attachment tree and update the cloner meshes, 0 means each tick */
	UPROPERTY(EditInstanceOnly, Setter, Getter, Category="Cloner", AdvancedDisplay, meta=(ClampMin="0"))
	float TreeUpdateInterval = 0.2f;

	/** Cloner instance seed for random deterministic patterns */
	UPROPERTY(EditInstanceOnly, Setter, Getter, Category="Cloner")
	int32 Seed = 0;

	/** Cloner color when unaffected by effectors, color will be passed down to the material (ParticleColor) */
	UPROPERTY(EditInstanceOnly, Setter, Getter, Category="Cloner")
	FLinearColor Color = FLinearColor::White;

	/** Name of the layout to use */
	UPROPERTY(EditInstanceOnly, Setter, Getter, DisplayName="Layout", Category="Layout", meta=(GetOptions="GetClonerLayoutNames"))
	FName LayoutName = NAME_None;

	/** Active layout used */
	UPROPERTY(Transient, DuplicateTransient, NonTransactional)
	TObjectPtr<UCEClonerLayoutBase> ActiveLayout;

	/** Active Extensions on this layout */
	UPROPERTY(Transient, DuplicateTransient, NonTransactional)
	TArray<TObjectPtr<UCEClonerExtensionBase>> ActiveExtensions;

	/** Layout instances cached */
	UPROPERTY()
	TArray<TObjectPtr<UCEClonerLayoutBase>> LayoutInstances;

	/** Layout extensions instances cached */
	UPROPERTY()
	TArray<TObjectPtr<UCEClonerExtensionBase>> ExtensionInstances;

#if WITH_EDITORONLY_DATA
	/** Toggle the sprite to visualize and click on this cloner */
	UPROPERTY(EditInstanceOnly, AdvancedDisplay, Category="Cloner", meta=(AllowPrivateAccess="true"))
	bool bVisualizerSpriteVisible = true;
#endif

private:
	static constexpr TCHAR SpriteTexturePath[] = TEXT("/Script/Engine.Texture2D'/ClonerEffector/Textures/T_ClonerIcon.T_ClonerIcon'");

	/** Initiate and perform operation */
	void InitializeCloner();
	void RegisterTicker();
	bool TickCloner(float InDelta);

	/**
	* Triggers an update of the attachment tree to detect updated items
	* If reset is true, clears the attachment tree and rebuilds it otherwise diff update
	*/
	void UpdateClonerAttachmentTree(bool bInReset = false);

	/** Called to trigger an update of cloner rendering state tree */
	void UpdateClonerRenderState();

	void OnEnabledChanged();
	void OnClonerEnabled();
	void OnClonerDisabled();
	void OnClonerSetEnabled(const UWorld* InWorld, bool bInEnabled, bool bInTransact);

	void OnSeedChanged();
	void OnColorChanged();
	void OnLayoutNameChanged();

#if WITH_EDITOR
	void OnVisualizerSpriteVisibleChanged();
#endif

	void BindActorDelegates(AActor* InActor);
	void UnbindActorDelegates(AActor* InActor) const;

	void SetActorVisibility(AActor* InActor, bool bInVisibility);

#if WITH_EDITOR
	void OnActorPropertyChanged(UObject* InObject, FPropertyChangedEvent& InPropertyChangedEvent);
	void OnMaterialCompiled(UMaterialInterface* InMaterial);
#endif

	/** Called when a cloned actor is destroyed */
	UFUNCTION()
	void OnActorDestroyed(AActor* InDestroyedActor);

	/** Called when a material has changed in any cloned actors */
	void OnMaterialChanged(UObject* InObject);

	/** Called when a mesh has changed in any cloned actors */
	void OnMeshChanged(UStaticMeshComponent*, AActor* InActor);

	/** Called when the render state of a cloned component changes */
	void OnRenderStateDirty(UActorComponent& InActorComponent);

	/** Called when the transform state of a cloned component changes */
	void OnComponentTransformed(USceneComponent* InComponent, EUpdateTransformFlags InFlags, ETeleportType InTeleport);

	template<
		typename InLayoutClass
		UE_REQUIRES(TIsDerivedFrom<InLayoutClass, UCEClonerLayoutBase>::Value)>
	InLayoutClass* FindOrAddLayout()
	{
		return Cast<InLayoutClass>(FindOrAddLayout(InLayoutClass::StaticClass()));
	}

	/** Find or add a layout by its class */
	UCEClonerLayoutBase* FindOrAddLayout(TSubclassOf<UCEClonerLayoutBase> InClass);

	/** Find or add a layout by its name */
	UCEClonerLayoutBase* FindOrAddLayout(FName InLayoutName);

	template<
		typename InExtensionClass
		UE_REQUIRES(TIsDerivedFrom<InExtensionClass, UCEClonerExtensionBase>::Value)>
	InExtensionClass* FindOrAddExtension()
	{
		return Cast<InExtensionClass>(FindOrAddExtension(InExtensionClass::StaticClass()));
	}

	/** Find or add an extension by its class */
	UCEClonerExtensionBase* FindOrAddExtension(TSubclassOf<UCEClonerExtensionBase> InClass);

	/** Find or add an extension by its name */
	UCEClonerExtensionBase* FindOrAddExtension(FName InExtensionName);

	/** Gets all layout names available */
	UFUNCTION()
	TArray<FName> GetClonerLayoutNames() const;

	/** Attachment tree view */
	UPROPERTY(Transient, DuplicateTransient, NonTransactional)
	FCEClonerAttachmentTree ClonerTree;

	UPROPERTY(Transient, DuplicateTransient, NonTransactional)
	FCEMeshBuilder MeshBuilder;

	/** Asset meshes needs update */
	bool bClonerMeshesDirty = true;

	/** State of the baked dynamic and static mesh creation */
	std::atomic<bool> bClonerMeshesUpdating = false;

	float TreeUpdateDeltaTime = 0.f;

	bool bNeedsRefresh = false;

	bool bClonerInitialized = false;

	FTSTicker::FDelegateHandle ClonerTickerHandle;

#if WITH_EDITOR
	double LastNotificationTime = 0.0;

	/** Used for PECP */
	static const TCEPropertyChangeDispatcher<UCEClonerComponent> PropertyChangeDispatcher;
#endif
};