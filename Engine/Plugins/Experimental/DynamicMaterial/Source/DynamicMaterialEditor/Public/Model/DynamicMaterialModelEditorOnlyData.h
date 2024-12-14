// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMEDefs.h"
#include "Misc/NotifyHook.h"
#include "Model/IDynamicMaterialModelEditorOnlyDataInterface.h"
#include "UObject/Object.h"

#include "Engine/EngineTypes.h"
#include "MaterialDomain.h"
#include "MaterialEditingLibrary.h"
#include "UObject/WeakObjectPtrFwd.h"

#include "DynamicMaterialModelEditorOnlyData.generated.h"

class UDMMaterialComponent;
class UDMMaterialParameter;
class UDMMaterialProperty;
class UDMMaterialSlot;
class UDMMaterialValue;
class UDMMaterialValueFloat1;
class UDMTextureSet;
class UDMTextureUV;
class UDynamicMaterialModel;
class UDynamicMaterialModelBase;
class UDynamicMaterialModelEditorOnlyData;
class UMaterialExpression;
struct FDMComponentPath;
struct FDMComponentPathSegment;
struct FDMMaterialBuildState;
struct FDMMaterialChannelListPreset;

DECLARE_MULTICAST_DELEGATE_OneParam(FDMOnMaterialBuilt, UDynamicMaterialModelBase*);
DECLARE_MULTICAST_DELEGATE_OneParam(FDMOnValueListUpdated, UDynamicMaterialModelBase*);
DECLARE_MULTICAST_DELEGATE_OneParam(FDMOnSlotListUpdated, UDynamicMaterialModelBase*);
DECLARE_MULTICAST_DELEGATE_OneParam(FDMOnPropertyUpdated, UDynamicMaterialModelBase*);

UENUM(BlueprintType)
enum class EDMState : uint8
{
	Idle,
	Building
};

UCLASS(MinimalAPI, BlueprintType, EditInlineNew, DefaultToInstanced, ClassGroup = "Material Designer")
class UDynamicMaterialModelEditorOnlyData : public UObject, public IDynamicMaterialModelEditorOnlyDataInterface, public FNotifyHook, public IDMBuildable
{
	GENERATED_BODY()

	friend class UDynamicMaterialModelFactory;
	friend class FDMMaterialModelPropertyRowGenerator;

public:
	DYNAMICMATERIALEDITOR_API static const FString SlotsPathToken;
	DYNAMICMATERIALEDITOR_API static const FString BaseColorSlotPathToken;
	DYNAMICMATERIALEDITOR_API static const FString EmissiveSlotPathToken;
	DYNAMICMATERIALEDITOR_API static const FString OpacitySlotPathToken;
	DYNAMICMATERIALEDITOR_API static const FString RoughnessPathToken;
	DYNAMICMATERIALEDITOR_API static const FString SpecularPathToken;
	DYNAMICMATERIALEDITOR_API static const FString MetallicPathToken;
	DYNAMICMATERIALEDITOR_API static const FString NormalPathToken;
	DYNAMICMATERIALEDITOR_API static const FString PixelDepthOffsetPathToken;
	DYNAMICMATERIALEDITOR_API static const FString WorldPositionOffsetPathToken;
	DYNAMICMATERIALEDITOR_API static const FString AmbientOcclusionPathToken;
	DYNAMICMATERIALEDITOR_API static const FString AnisotropyPathToken;
	DYNAMICMATERIALEDITOR_API static const FString RefractionPathToken;
	DYNAMICMATERIALEDITOR_API static const FString TangentPathToken;
	DYNAMICMATERIALEDITOR_API static const FString DisplacementPathToken;
	DYNAMICMATERIALEDITOR_API static const FString SubsurfaceColorPathToken;
	DYNAMICMATERIALEDITOR_API static const FString SurfaceThicknessPathToken;
	DYNAMICMATERIALEDITOR_API static const FString Custom1PathToken;
	DYNAMICMATERIALEDITOR_API static const FString Custom2PathToken;
	DYNAMICMATERIALEDITOR_API static const FString Custom3PathToken;
	DYNAMICMATERIALEDITOR_API static const FString Custom4PathToken;
	DYNAMICMATERIALEDITOR_API static const FString PropertiesPathToken;

	DYNAMICMATERIALEDITOR_API static const TArray<EMaterialDomain> SupportedDomains;
	DYNAMICMATERIALEDITOR_API static const TArray<EBlendMode> SupportedBlendModes;

	DYNAMICMATERIALEDITOR_API static const FName AlphaValueName;

	DYNAMICMATERIALEDITOR_API static UDynamicMaterialModelEditorOnlyData* Get(UDynamicMaterialModelBase* InModelBase);
	DYNAMICMATERIALEDITOR_API static UDynamicMaterialModelEditorOnlyData* Get(const TWeakObjectPtr<UDynamicMaterialModelBase>& InModelBaseWeak);
	DYNAMICMATERIALEDITOR_API static UDynamicMaterialModelEditorOnlyData* Get(UDynamicMaterialModel* InModel);
	DYNAMICMATERIALEDITOR_API static UDynamicMaterialModelEditorOnlyData* Get(const TWeakObjectPtr<UDynamicMaterialModel>& InModelWeak);
	DYNAMICMATERIALEDITOR_API static UDynamicMaterialModelEditorOnlyData* Get(const TScriptInterface<IDynamicMaterialModelEditorOnlyDataInterface>& InInterface);
	DYNAMICMATERIALEDITOR_API static UDynamicMaterialModelEditorOnlyData* Get(IDynamicMaterialModelEditorOnlyDataInterface* InInterface);
	DYNAMICMATERIALEDITOR_API static UDynamicMaterialModelEditorOnlyData* Get(UDynamicMaterialInstance* InInstance);

	UDynamicMaterialModelEditorOnlyData();

	void Initialize();

	UFUNCTION(BlueprintPure, Category = "Material Designer")
	UDynamicMaterialModel* GetMaterialModel() const { return MaterialModel; }

	UFUNCTION(BlueprintPure, Category = "Material Designer")
	DYNAMICMATERIALEDITOR_API UMaterial* GetGeneratedMaterial() const;

	UFUNCTION(BlueprintPure, Category = "Material Designer")
	EDMState GetState() const { return State; }

	UFUNCTION(BlueprintPure, Category = "Material Designer")
	TEnumAsByte<EMaterialDomain> GetDomain() const { return Domain; }

	UFUNCTION(BlueprintCallable, Category = "Material Designer")
	DYNAMICMATERIALEDITOR_API void SetDomain(TEnumAsByte<EMaterialDomain> InDomain);

	UFUNCTION(BlueprintPure, Category = "Material Designer")
	TEnumAsByte<EBlendMode> GetBlendMode() const { return BlendMode; }

	UFUNCTION(BlueprintCallable, Category = "Material Designer")
	DYNAMICMATERIALEDITOR_API void SetBlendMode(TEnumAsByte<EBlendMode> InBlendMode);

	UFUNCTION(BlueprintPure, Category = "Material Designer")
	EDMMaterialShadingModel GetShadingModel() const { return ShadingModel; }

	UFUNCTION(BlueprintCallable, Category = "Material Designer")
	DYNAMICMATERIALEDITOR_API void SetShadingModel(EDMMaterialShadingModel InShadingModel);

	UFUNCTION(BlueprintPure, Category = "Material Designer")
	DYNAMICMATERIALEDITOR_API bool GetHasPixelAnimation() const;

	UFUNCTION(BlueprintCallable, Category = "Material Designer")
	DYNAMICMATERIALEDITOR_API void SetHasPixelAnimation(bool bInHasAnimation);

	UFUNCTION(BlueprintPure, Category = "Material Designer")
	DYNAMICMATERIALEDITOR_API bool GetIsTwoSided() const;

	UFUNCTION(BlueprintCallable, Category = "Material Designer")
	DYNAMICMATERIALEDITOR_API void SetIsTwoSided(bool bInEnabled);

	UFUNCTION(BlueprintPure, Category = "Material Designer")
	DYNAMICMATERIALEDITOR_API bool IsOutputTranslucentVelocityEnabled() const;

	UFUNCTION(BlueprintCallable, Category = "Material Designer")
	DYNAMICMATERIALEDITOR_API void SetOutputTranslucentVelocityEnabled(bool bInEnabled);

	UFUNCTION(BlueprintPure, Category = "Material Designer")
	DYNAMICMATERIALEDITOR_API bool IsNaniteTessellationEnabled() const;

	UFUNCTION(BlueprintCallable, Category = "Material Designer")
	DYNAMICMATERIALEDITOR_API void SetNaniteTessellationEnabled(bool bInEnabled);

	UFUNCTION(BlueprintPure, Category = "Material Designer")
	DYNAMICMATERIALEDITOR_API bool IsResponsiveAAEnabled() const;

	UFUNCTION(BlueprintCallable, Category = "Material Designer")
	DYNAMICMATERIALEDITOR_API void SetResponsiveAAEnabled(bool bInEnabled);

	UFUNCTION(BlueprintCallable, Category = "Material Designer")
	DYNAMICMATERIALEDITOR_API void SetChannelListPreset(FName InPresetName);

	UFUNCTION(BlueprintPure, Category = "Material Designer")
	const FMaterialStatistics& GetMaterialStats() const;

	UFUNCTION(BlueprintCallable, Category = "Material Designer")
	DYNAMICMATERIALEDITOR_API void OpenMaterialEditor() const;

	UFUNCTION(BlueprintPure, Category = "Material Designer")
	DYNAMICMATERIALEDITOR_API TMap<EDMMaterialPropertyType, UDMMaterialProperty*> GetMaterialProperties() const;

	UFUNCTION(BlueprintPure, Category = "Material Designer")
	DYNAMICMATERIALEDITOR_API UDMMaterialProperty* GetMaterialProperty(EDMMaterialPropertyType InMaterialProperty) const;

	UFUNCTION(BlueprintPure, Category = "Material Designer")
	const TArray<UDMMaterialSlot*>& GetSlots() const { return Slots; }

	/** Gets slot by index. Highly recommended to use GetSlotForMaterialProperty(PropertyType). */
	UFUNCTION(BlueprintPure, Category = "Material Designer")
	DYNAMICMATERIALEDITOR_API UDMMaterialSlot* GetSlot(int32 Index) const;

	UFUNCTION(BlueprintPure, Category = "Material Designer")
	DYNAMICMATERIALEDITOR_API UDMMaterialSlot* GetSlotForMaterialProperty(EDMMaterialPropertyType InType) const;

	/** Same as the above method, but will only return the slot if the material property is enabled. */
	UFUNCTION(BlueprintPure, Category = "Material Designer")
	DYNAMICMATERIALEDITOR_API UDMMaterialSlot* GetSlotForEnabledMaterialProperty(EDMMaterialPropertyType InType) const;

	/** Adds the next available slot. Highly recommended to use AddSlotForMaterialProperty(PropertyType). */
	UFUNCTION(BlueprintCallable, Category = "Material Designer")
	DYNAMICMATERIALEDITOR_API UDMMaterialSlot* AddSlot();

	UFUNCTION(BlueprintCallable, Category = "Material Designer")
	DYNAMICMATERIALEDITOR_API UDMMaterialSlot* AddSlotForMaterialProperty(EDMMaterialPropertyType InType);

	/** Removes the next slot by index. Highly recommended to use RemoveSlotForMaterialProperty(PropertyType). */
	UFUNCTION(BlueprintCallable, Category = "Material Designer")
	DYNAMICMATERIALEDITOR_API UDMMaterialSlot* RemoveSlot(int32 Index);

	UFUNCTION(BlueprintCallable, Category = "Material Designer")
	DYNAMICMATERIALEDITOR_API UDMMaterialSlot* RemoveSlotForMaterialProperty(EDMMaterialPropertyType InType);

	UFUNCTION(BlueprintPure, Category = "Material Designer")
	DYNAMICMATERIALEDITOR_API TArray<EDMMaterialPropertyType> GetMaterialPropertiesForSlot(const UDMMaterialSlot* InSlot) const;

	UFUNCTION(BlueprintCallable, Category = "Material Designer")
	DYNAMICMATERIALEDITOR_API void AssignMaterialPropertyToSlot(EDMMaterialPropertyType InProperty, UDMMaterialSlot* InSlot);

	UFUNCTION(BlueprintCallable, Category = "Material Designer")
	DYNAMICMATERIALEDITOR_API void UnassignMaterialProperty(EDMMaterialPropertyType InProperty);
	
	FDMOnMaterialBuilt::RegistrationType& GetOnMaterialBuiltDelegate() { return OnMaterialBuiltDelegate; }
	FDMOnValueListUpdated::RegistrationType& GetOnValueListUpdateDelegate() { return OnValueListUpdateDelegate; }
	FDMOnSlotListUpdated::RegistrationType& GetOnSlotListUpdateDelegate() { return OnSlotListUpdateDelegate; }
	FDMOnPropertyUpdated::RegistrationType& GetOnPropertyUpdateDelegate() { return OnPropertyUpdateDelegate; }

	void OnPropertyUpdate(UDMMaterialProperty* InProperty);

	DYNAMICMATERIALEDITOR_API TSharedRef<FDMMaterialBuildState> CreateBuildState(UMaterial* InMaterialToBuild, bool bInDirtyAssets = true) const;

	/**
	 * Integrates a Texture Set with the model.
	 * @param InTextureSet The set to integrate.
	 * @param bInReplaceSlots Whether to add to or completely replace slots.
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, Category = "Material Designer")
	DYNAMICMATERIALEDITOR_API bool AddTextureSet(UDMTextureSet* InTextureSet, bool bInReplaceSlots);

	bool NeedsWizard() const;

	DYNAMICMATERIALEDITOR_API void OnWizardComplete();

	void SaveEditor();

	//~ Begin FNotifyHook
	DYNAMICMATERIALEDITOR_API virtual void NotifyPostChange(const FPropertyChangedEvent& InPropertyChangedEvent, class FEditPropertyChain* InPropertyThatChanged);
	//~ End FNotifyHook

	//~ Begin UObject
	DYNAMICMATERIALEDITOR_API virtual void PostLoad() override;
	DYNAMICMATERIALEDITOR_API virtual void PostEditUndo() override;
	DYNAMICMATERIALEDITOR_API virtual void PostEditImport() override;
	DYNAMICMATERIALEDITOR_API virtual void PostDuplicate(bool bInDuplicateForPIE) override;
	DYNAMICMATERIALEDITOR_API virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& InPropertyChangedEvent) override;
	//~ End UObject

	//~ Begin IDMBuildable
	virtual void DoBuild_Implementation(bool bInDirtyAssets) override;
	//~ End IDMBuildable

	//~ Begin IDynamicMaterialModelEditorOnlyDataInterface
	DYNAMICMATERIALEDITOR_API virtual void PostEditorDuplicate() override;
	DYNAMICMATERIALEDITOR_API virtual void RequestMaterialBuild(bool bInImmediate = false) override;
	DYNAMICMATERIALEDITOR_API virtual void OnValueListUpdate() override;
	DYNAMICMATERIALEDITOR_API virtual void OnValueUpdated(UDMMaterialValue* InValue, EDMUpdateType InUpdateType) override;
	DYNAMICMATERIALEDITOR_API virtual void OnTextureUVUpdated(UDMTextureUV* InTextureUV) override;
	DYNAMICMATERIALEDITOR_API virtual TSharedRef<IDMMaterialBuildStateInterface> CreateBuildStateInterface(UMaterial* InMaterialToBuild) const override;
	DYNAMICMATERIALEDITOR_API virtual void SetPropertyComponent(EDMMaterialPropertyType InPropertyType, FName InComponentName, UDMMaterialComponent* InComponent) override;
	DYNAMICMATERIALEDITOR_API virtual UDMMaterialComponent* GetSubComponentByPath(FDMComponentPath& InPath) const override;
	DYNAMICMATERIALEDITOR_API virtual UDMMaterialComponent* GetSubComponentByPath(FDMComponentPath& InPath, const FDMComponentPathSegment& InPathSegment) const override;
	//~ End IDynamicMaterialModelEditorOnlyDataInterface

protected:
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Material Designer")
	TObjectPtr<UDynamicMaterialModel> MaterialModel;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, TextExportTransient, Category = "Material Designer")
	EDMState State;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Material Designer", 
		meta = (ValidEnumValues = "MD_Surface,MD_PostProcess,MD_DeferredDecal,MD_LightFunction"))
	TEnumAsByte<EMaterialDomain> Domain;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Material Designer",
		meta = (ValidEnumValues = "BLEND_Opaque,BLEND_Translucent,BLEND_Masked,BLEND_Additive,BLEND_Modulate"))
	TEnumAsByte<EBlendMode> BlendMode;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Material Designer", meta = (ValidEnumValues = "Unlit,DefaultLit"))
	EDMMaterialShadingModel ShadingModel;

	/**
	 * Whether the opaque material has any pixel animations happening, that isn't included in the geometric velocities.
	 * This allows to disable renderer's heuristics that assumes animation is fully described with motion vector, such as TSR's anti-flickering heuristic.
	 */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Material Designer")
	bool bHasPixelAnimation;

	/** Indicates that the material should be rendered without backface culling and the normal should be flipped for backfaces. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Material Designer")
	bool bTwoSided;

	/** When true, translucent materials will output motion vectors and write to depth buffer in velocity pass. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Material Designer", meta = (DisplayName = "Output Translucent Velocity"))
	bool bOutputTranslucentVelocityEnabled;

	/** Whether tessellation is enabled on the material. NOTE: Required for displacement to work. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Material Designer", meta = (DisplayName = "Nanite Tessellation"))
	bool bNaniteTessellationEnabled;

	/**
	 * Indicates that the material should be rendered using responsive anti-aliasing. Improves sharpness of small moving particles such as sparks.
	 * Only use for small moving features because it will cause aliasing of the background.
	 */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Material Designer", meta = (DisplayName = "Responsive AA"))
	bool bResponsiveAAEnabled;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, TextExportTransient, Category = "Material Designer")
	TMap<EDMMaterialPropertyType, TObjectPtr<UDMMaterialSlot>> PropertySlotMap;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Instanced, Category = "Material Designer")
	TArray<TObjectPtr<UDMMaterialSlot>> Slots;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Instanced, Category = "Material Designer")
	TArray<TObjectPtr<UMaterialExpression>> Expressions;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Material Designer")
	bool bCreateMaterialPackage;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Material Designer")
	FMaterialStatistics MaterialStats;

	FDMOnMaterialBuilt OnMaterialBuiltDelegate;
	FDMOnValueListUpdated OnValueListUpdateDelegate;
	FDMOnSlotListUpdated OnSlotListUpdateDelegate;
	FDMOnPropertyUpdated OnPropertyUpdateDelegate;

	UPROPERTY(BlueprintReadOnly, Instanced, Category = "Material Designer")
	TObjectPtr<UDMMaterialProperty> BaseColor;

	UPROPERTY(BlueprintReadOnly, Instanced, Category = "Material Designer")
	TObjectPtr<UDMMaterialProperty> EmissiveColor;

	UPROPERTY(BlueprintReadOnly, Instanced, Category = "Material Designer")
	TObjectPtr<UDMMaterialProperty> Opacity;

	UPROPERTY(BlueprintReadOnly, Instanced, Category = "Material Designer")
	TObjectPtr<UDMMaterialProperty> OpacityMask;

	UPROPERTY(BlueprintReadOnly, Instanced, Category = "Material Designer")
	TObjectPtr<UDMMaterialProperty> Roughness;

	UPROPERTY(BlueprintReadOnly, Instanced, Category = "Material Designer")
	TObjectPtr<UDMMaterialProperty> Specular;

	UPROPERTY(BlueprintReadOnly, Instanced, Category = "Material Designer")
	TObjectPtr<UDMMaterialProperty> Metallic;

	UPROPERTY(BlueprintReadOnly, Instanced, Category = "Material Designer")
	TObjectPtr<UDMMaterialProperty> Normal;

	UPROPERTY(BlueprintReadOnly, Instanced, Category = "Material Designer")
	TObjectPtr<UDMMaterialProperty> PixelDepthOffset;

	UPROPERTY(BlueprintReadOnly, Instanced, Category = "Material Designer")
	TObjectPtr<UDMMaterialProperty> WorldPositionOffset;

	UPROPERTY(BlueprintReadOnly, Instanced, Category = "Material Designer")
	TObjectPtr<UDMMaterialProperty> AmbientOcclusion;

	UPROPERTY(BlueprintReadOnly, Instanced, Category = "Material Designer")
	TObjectPtr<UDMMaterialProperty> Anisotropy;

	UPROPERTY(BlueprintReadOnly, Instanced, Category = "Material Designer")
	TObjectPtr<UDMMaterialProperty> Refraction;

	UPROPERTY(BlueprintReadOnly, Instanced, Category = "Material Designer")
	TObjectPtr<UDMMaterialProperty> Tangent;

	UPROPERTY(BlueprintReadOnly, Instanced, Category = "Material Designer")
	TObjectPtr<UDMMaterialProperty> Displacement;

	UPROPERTY(BlueprintReadOnly, Instanced, Category = "Material Designer")
	TObjectPtr<UDMMaterialProperty> SubsurfaceColor;

	UPROPERTY(BlueprintReadOnly, Instanced, Category = "Material Designer")
	TObjectPtr<UDMMaterialProperty> SurfaceThickness;

	UPROPERTY(BlueprintReadOnly, Instanced, Category = "Material Designer")
	TObjectPtr<UDMMaterialProperty> Custom1;

	UPROPERTY(BlueprintReadOnly, Instanced, Category = "Material Designer")
	TObjectPtr<UDMMaterialProperty> Custom2;

	UPROPERTY(BlueprintReadOnly, Instanced, Category = "Material Designer")
	TObjectPtr<UDMMaterialProperty> Custom3;

	UPROPERTY(BlueprintReadOnly, Instanced, Category = "Material Designer")
	TObjectPtr<UDMMaterialProperty> Custom4;

	void CreateMaterial();

	UFUNCTION(BlueprintCallable, Category = "Material Designer")
	void BuildMaterial(bool bInDirtyAssets);

	FString GetMaterialAssetPath() const;
	FString GetMaterialAssetName() const;
	FString GetMaterialPackageName(const FString& InMaterialBaseName) const;

	void OnSlotConnectorsUpdated(UDMMaterialSlot* InSlot);

	/** Swaps the material properties from one slot to another, unless both slots exist and/or are the same. */
	void SwapSlotMaterialProperty(EDMMaterialPropertyType InPropertyFrom, EDMMaterialPropertyType InPropertyTo);

	/** 
	 * Swaps the material properties from one slot to another, unless both slots exist and/or are the same. 
	 * Ensuring that the To Property exists.
	 */
	void EnsureSwapSlotMaterialProperty(EDMMaterialPropertyType InPropertyFrom, EDMMaterialPropertyType InPropertyTo);

	void AssignPropertyAlphaValues();

	void OnDomainChanged();
	void OnBlendModeChanged();
	void OnShadingModelChanged();
	void OnMaterialFlagChanged();

	//~ Begin IDynamicMaterialModelEditorOnlyDataInterface
	virtual void ReinitComponents() override;
	//~ End IDynamicMaterialModelEditorOnlyDataInterface
};
