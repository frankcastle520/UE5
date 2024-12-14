// Copyright Epic Games, Inc. All Rights Reserved.

#include "Model/DynamicMaterialModelEditorOnlyData.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Components/DMMaterialLayer.h"
#include "Components/DMMaterialProperty.h"
#include "Components/DMMaterialSlot.h"
#include "Components/DMMaterialStageBlend.h"
#include "Components/DMMaterialSubStage.h"
#include "Components/DMMaterialValue.h"
#include "Components/DMTextureUV.h"
#include "Components/MaterialProperties/DMMPAmbientOcclusion.h"
#include "Components/MaterialProperties/DMMPAnisotropy.h"
#include "Components/MaterialProperties/DMMPBaseColor.h"
#include "Components/MaterialProperties/DMMPDisplacement.h"
#include "Components/MaterialProperties/DMMPEmissiveColor.h"
#include "Components/MaterialProperties/DMMPMetallic.h"
#include "Components/MaterialProperties/DMMPNormal.h"
#include "Components/MaterialProperties/DMMPOpacity.h"
#include "Components/MaterialProperties/DMMPOpacityMask.h"
#include "Components/MaterialProperties/DMMPPixelDepthOffset.h"
#include "Components/MaterialProperties/DMMPRefraction.h"
#include "Components/MaterialProperties/DMMPRoughness.h"
#include "Components/MaterialProperties/DMMPSpecular.h"
#include "Components/MaterialProperties/DMMPSubsurfaceColor.h"
#include "Components/MaterialProperties/DMMPSurfaceThickness.h"
#include "Components/MaterialProperties/DMMPTangent.h"
#include "Components/MaterialProperties/DMMPWorldPositionOffset.h"
#include "Components/MaterialStageExpressions/DMMSETextureSample.h"
#include "Components/MaterialStageInputs/DMMSIExpression.h"
#include "Components/MaterialStageInputs/DMMSIValue.h"
#include "Components/MaterialValues/DMMaterialValueTexture.h"
#include "CoreGlobals.h"
#include "DMComponentPath.h"
#include "DMDefs.h"
#include "DMTextureSet.h"
#include "DynamicMaterialEditorModule.h"
#include "DynamicMaterialEditorSettings.h"
#include "DynamicMaterialModule.h"
#include "Factories/MaterialFactoryNew.h"
#include "FileHelpers.h"
#include "IAssetTools.h"
#include "Material/DynamicMaterialInstance.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialFunction.h"
#include "Misc/Guid.h"
#include "Model/DMMaterialBuildState.h"
#include "Model/DynamicMaterialModel.h"
#include "UObject/Package.h"
#include "Utils/DMBuildRequestSubsystem.h"
#include "Utils/DMUtils.h"

#define LOCTEXT_NAMESPACE "MaterialDesignerModel"

const FString UDynamicMaterialModelEditorOnlyData::SlotsPathToken               = FString(TEXT("Slots"));
const FString UDynamicMaterialModelEditorOnlyData::BaseColorSlotPathToken       = FString(TEXT("BaseColor"));
const FString UDynamicMaterialModelEditorOnlyData::EmissiveSlotPathToken        = FString(TEXT("Emissive"));
const FString UDynamicMaterialModelEditorOnlyData::OpacitySlotPathToken         = FString(TEXT("Opacity"));
const FString UDynamicMaterialModelEditorOnlyData::RoughnessPathToken           = FString(TEXT("Roughness"));
const FString UDynamicMaterialModelEditorOnlyData::SpecularPathToken            = FString(TEXT("Specular"));
const FString UDynamicMaterialModelEditorOnlyData::MetallicPathToken            = FString(TEXT("Metallic"));
const FString UDynamicMaterialModelEditorOnlyData::NormalPathToken              = FString(TEXT("Normal"));
const FString UDynamicMaterialModelEditorOnlyData::PixelDepthOffsetPathToken    = FString(TEXT("PDO"));
const FString UDynamicMaterialModelEditorOnlyData::WorldPositionOffsetPathToken = FString(TEXT("WPO"));
const FString UDynamicMaterialModelEditorOnlyData::AmbientOcclusionPathToken    = FString(TEXT("AO"));
const FString UDynamicMaterialModelEditorOnlyData::AnisotropyPathToken          = FString(TEXT("Anisotropy"));
const FString UDynamicMaterialModelEditorOnlyData::RefractionPathToken          = FString(TEXT("Refraction"));
const FString UDynamicMaterialModelEditorOnlyData::TangentPathToken             = FString(TEXT("Tangent"));
const FString UDynamicMaterialModelEditorOnlyData::DisplacementPathToken        = FString(TEXT("Displacement"));
const FString UDynamicMaterialModelEditorOnlyData::SubsurfaceColorPathToken     = FString(TEXT("SubsurfaceColor"));
const FString UDynamicMaterialModelEditorOnlyData::SurfaceThicknessPathToken    = FString(TEXT("SurfaceThickness"));
const FString UDynamicMaterialModelEditorOnlyData::Custom1PathToken             = FString(TEXT("Custom1"));
const FString UDynamicMaterialModelEditorOnlyData::Custom2PathToken             = FString(TEXT("Custom2"));
const FString UDynamicMaterialModelEditorOnlyData::Custom3PathToken             = FString(TEXT("Custom3"));
const FString UDynamicMaterialModelEditorOnlyData::Custom4PathToken             = FString(TEXT("Custom4"));
const FString UDynamicMaterialModelEditorOnlyData::PropertiesPathToken          = FString(TEXT("Properties"));

namespace UE::DynamicMaterialEditor::Private
{
	const TMap<FString, EDMMaterialPropertyType> TokenToPropertyMap = {
		{UDynamicMaterialModelEditorOnlyData::BaseColorSlotPathToken,       EDMMaterialPropertyType::BaseColor},
		{UDynamicMaterialModelEditorOnlyData::EmissiveSlotPathToken,        EDMMaterialPropertyType::EmissiveColor},
		{UDynamicMaterialModelEditorOnlyData::OpacitySlotPathToken,         EDMMaterialPropertyType::Opacity},
		{UDynamicMaterialModelEditorOnlyData::RoughnessPathToken,           EDMMaterialPropertyType::Roughness},
		{UDynamicMaterialModelEditorOnlyData::SpecularPathToken,            EDMMaterialPropertyType::Specular},
		{UDynamicMaterialModelEditorOnlyData::MetallicPathToken,            EDMMaterialPropertyType::Metallic},
		{UDynamicMaterialModelEditorOnlyData::NormalPathToken,              EDMMaterialPropertyType::Normal},
		{UDynamicMaterialModelEditorOnlyData::PixelDepthOffsetPathToken,    EDMMaterialPropertyType::PixelDepthOffset},
		{UDynamicMaterialModelEditorOnlyData::WorldPositionOffsetPathToken, EDMMaterialPropertyType::WorldPositionOffset},
		{UDynamicMaterialModelEditorOnlyData::AmbientOcclusionPathToken,    EDMMaterialPropertyType::AmbientOcclusion},
		{UDynamicMaterialModelEditorOnlyData::AnisotropyPathToken,          EDMMaterialPropertyType::Anisotropy},
		{UDynamicMaterialModelEditorOnlyData::RefractionPathToken,          EDMMaterialPropertyType::Refraction},
		{UDynamicMaterialModelEditorOnlyData::TangentPathToken,             EDMMaterialPropertyType::Tangent},
		{UDynamicMaterialModelEditorOnlyData::DisplacementPathToken,        EDMMaterialPropertyType::Displacement},
		{UDynamicMaterialModelEditorOnlyData::SubsurfaceColorPathToken,     EDMMaterialPropertyType::SubsurfaceColor},
		{UDynamicMaterialModelEditorOnlyData::SurfaceThicknessPathToken,    EDMMaterialPropertyType::SurfaceThickness},
		{UDynamicMaterialModelEditorOnlyData::Custom1PathToken,             EDMMaterialPropertyType::Custom1},
		{UDynamicMaterialModelEditorOnlyData::Custom2PathToken,             EDMMaterialPropertyType::Custom2},
		{UDynamicMaterialModelEditorOnlyData::Custom3PathToken,             EDMMaterialPropertyType::Custom3},
		{UDynamicMaterialModelEditorOnlyData::Custom4PathToken,             EDMMaterialPropertyType::Custom4}
	};
}

const FName UDynamicMaterialModelEditorOnlyData::AlphaValueName = TEXT("AlphaValue");

const TArray<EMaterialDomain> UDynamicMaterialModelEditorOnlyData::SupportedDomains =
{
	EMaterialDomain::MD_Surface,
	EMaterialDomain::MD_PostProcess,
	EMaterialDomain::MD_DeferredDecal,
	EMaterialDomain::MD_LightFunction
};

const TArray<EBlendMode> UDynamicMaterialModelEditorOnlyData::SupportedBlendModes =
{
	EBlendMode::BLEND_Opaque,
	EBlendMode::BLEND_Masked,
	EBlendMode::BLEND_Translucent,
	EBlendMode::BLEND_Additive,
	EBlendMode::BLEND_Modulate
};

UDynamicMaterialModelEditorOnlyData* UDynamicMaterialModelEditorOnlyData::Get(UDynamicMaterialModelBase* InModelBase)
{
	if (InModelBase)
	{
		return Get(InModelBase->ResolveMaterialModel());
	}

	return nullptr;
}

UDynamicMaterialModelEditorOnlyData* UDynamicMaterialModelEditorOnlyData::Get(const TWeakObjectPtr<UDynamicMaterialModelBase>& InModelBaseWeak)
{
	return Get(InModelBaseWeak.Get());
}

UDynamicMaterialModelEditorOnlyData* UDynamicMaterialModelEditorOnlyData::Get(UDynamicMaterialModel* InModel)
{
	if (InModel)
	{
		return Get(InModel->GetEditorOnlyData());
	}

	return nullptr;
}

UDynamicMaterialModelEditorOnlyData* UDynamicMaterialModelEditorOnlyData::Get(const TWeakObjectPtr<UDynamicMaterialModel>& InModelWeak)
{
	return Get(InModelWeak.Get());
}

UDynamicMaterialModelEditorOnlyData* UDynamicMaterialModelEditorOnlyData::Get(const TScriptInterface<IDynamicMaterialModelEditorOnlyDataInterface>& InInterface)
{
	return Cast<UDynamicMaterialModelEditorOnlyData>(InInterface.GetObject());
}

UDynamicMaterialModelEditorOnlyData* UDynamicMaterialModelEditorOnlyData::Get(IDynamicMaterialModelEditorOnlyDataInterface* InInterface)
{
	return Cast<UDynamicMaterialModelEditorOnlyData>(InInterface);
}

UDynamicMaterialModelEditorOnlyData* UDynamicMaterialModelEditorOnlyData::Get(UDynamicMaterialInstance* InInstance)
{
	if (InInstance)
	{
		return Get(InInstance->GetMaterialModel());
	}

	return nullptr;
}

UDynamicMaterialModelEditorOnlyData::UDynamicMaterialModelEditorOnlyData()
	: State(EDMState::Idle)
	, Domain(EMaterialDomain::MD_Surface)
	, BlendMode(EBlendMode::BLEND_Opaque)
	, ShadingModel(EDMMaterialShadingModel::DefaultLit)
	, bHasPixelAnimation(false)
	, bTwoSided(true)
	, bOutputTranslucentVelocityEnabled(true)
	, bNaniteTessellationEnabled(true)
	, bResponsiveAAEnabled(true)
{
	BaseColor =           CreateDefaultSubobject<UDMMaterialPropertyBaseColor>(          "MaterialProperty_BaseColor");
	EmissiveColor =       CreateDefaultSubobject<UDMMaterialPropertyEmissiveColor>(      "MaterialProperty_EmissiveColor");
	Opacity =             CreateDefaultSubobject<UDMMaterialPropertyOpacity>(            "MaterialProperty_Opacity");
	OpacityMask =         CreateDefaultSubobject<UDMMaterialPropertyOpacityMask>(        "MaterialProperty_OpacityMask");
	Roughness =           CreateDefaultSubobject<UDMMaterialPropertyRoughness>(          "MaterialProperty_Roughness");
	Specular =            CreateDefaultSubobject<UDMMaterialPropertySpecular>(           "MaterialProperty_Specular");
	Metallic =            CreateDefaultSubobject<UDMMaterialPropertyMetallic>(           "MaterialProperty_Metallic");
	Normal =              CreateDefaultSubobject<UDMMaterialPropertyNormal>(             "MaterialProperty_Normal");
	PixelDepthOffset =    CreateDefaultSubobject<UDMMaterialPropertyPixelDepthOffset>(   "MaterialProperty_PixelDepthOffset");
	WorldPositionOffset = CreateDefaultSubobject<UDMMaterialPropertyWorldPositionOffset>("MaterialProperty_WorldPositionOffset");
	AmbientOcclusion =    CreateDefaultSubobject<UDMMaterialPropertyAmbientOcclusion>(   "MaterialProperty_AmbientOcclusion");
	Anisotropy =          CreateDefaultSubobject<UDMMaterialPropertyAnisotropy>(         "MaterialProperty_Anisotropy");
	Refraction =          CreateDefaultSubobject<UDMMaterialPropertyRefraction>(         "MaterialProperty_Refraction");
	Tangent =             CreateDefaultSubobject<UDMMaterialPropertyTangent>(            "MaterialProperty_Tangent");
	Displacement =        CreateDefaultSubobject<UDMMaterialPropertyDisplacement>(       "MaterialProperty_Displacement");
	SubsurfaceColor =     CreateDefaultSubobject<UDMMaterialPropertySubsurfaceColor>(    "MaterialProperty_SubsurfaceColor");
	SurfaceThickness =    CreateDefaultSubobject<UDMMaterialPropertySurfaceThickness>(   "MaterialProperty_SurfaceThickness");

	Custom1 = UDMMaterialProperty::CreateCustomMaterialPropertyDefaultSubobject(this, EDMMaterialPropertyType::Custom1, "MaterialProperty_Custom1");
	Custom2 = UDMMaterialProperty::CreateCustomMaterialPropertyDefaultSubobject(this, EDMMaterialPropertyType::Custom2, "MaterialProperty_Custom2");
	Custom3 = UDMMaterialProperty::CreateCustomMaterialPropertyDefaultSubobject(this, EDMMaterialPropertyType::Custom3, "MaterialProperty_Custom3");
	Custom4 = UDMMaterialProperty::CreateCustomMaterialPropertyDefaultSubobject(this, EDMMaterialPropertyType::Custom4, "MaterialProperty_Custom4");

	UE::DynamicMaterial::ForEachMaterialPropertyType(
		[this](EDMMaterialPropertyType InType)
		{
			if (UDMMaterialProperty* Property = GetMaterialProperty(InType))
			{
				Property->SetComponentState(EDMComponentLifetimeState::Added);
			}

			return EDMIterationResult::Continue;
		}
	);
}

void UDynamicMaterialModelEditorOnlyData::AssignPropertyAlphaValues()
{
	BaseColor          ->AddComponent(AlphaValueName, MaterialModel->GetGlobalParameterValue(UDynamicMaterialModel::GlobalBaseColorValueName));
	EmissiveColor      ->AddComponent(AlphaValueName, MaterialModel->GetGlobalParameterValue(UDynamicMaterialModel::GlobalEmissiveColorValueName));
	Opacity            ->AddComponent(AlphaValueName, MaterialModel->GetGlobalParameterValue(UDynamicMaterialModel::GlobalOpacityValueName));
	OpacityMask        ->AddComponent(AlphaValueName, MaterialModel->GetGlobalParameterValue(UDynamicMaterialModel::GlobalOpacityValueName));
	Metallic           ->AddComponent(AlphaValueName, MaterialModel->GetGlobalParameterValue(UDynamicMaterialModel::GlobalMetallicValueName));
	Specular           ->AddComponent(AlphaValueName, MaterialModel->GetGlobalParameterValue(UDynamicMaterialModel::GlobalSpecularValueName));
	Roughness          ->AddComponent(AlphaValueName, MaterialModel->GetGlobalParameterValue(UDynamicMaterialModel::GlobalRoughnessValueName));
	Normal             ->AddComponent(AlphaValueName, MaterialModel->GetGlobalParameterValue(UDynamicMaterialModel::GlobalNormalValueName));
	Anisotropy         ->AddComponent(AlphaValueName, MaterialModel->GetGlobalParameterValue(UDynamicMaterialModel::GlobalAnisotropyValueName));
	WorldPositionOffset->AddComponent(AlphaValueName, MaterialModel->GetGlobalParameterValue(UDynamicMaterialModel::GlobalWorldPositionOffsetValueName));
	AmbientOcclusion   ->AddComponent(AlphaValueName, MaterialModel->GetGlobalParameterValue(UDynamicMaterialModel::GlobalAmbientOcclusionValueName));
	Refraction         ->AddComponent(AlphaValueName, MaterialModel->GetGlobalParameterValue(UDynamicMaterialModel::GlobalRefractionValueName));
	Tangent            ->AddComponent(AlphaValueName, MaterialModel->GetGlobalParameterValue(UDynamicMaterialModel::GlobalTangentValueName));
	PixelDepthOffset   ->AddComponent(AlphaValueName, MaterialModel->GetGlobalParameterValue(UDynamicMaterialModel::GlobalPixelDepthOffsetValueName));
	Displacement       ->AddComponent(AlphaValueName, MaterialModel->GetGlobalParameterValue(UDynamicMaterialModel::GlobalDisplacementValueName));
	SubsurfaceColor    ->AddComponent(AlphaValueName, MaterialModel->GetGlobalParameterValue(UDynamicMaterialModel::GlobalSubsurfaceColorValueName));
	SurfaceThickness   ->AddComponent(AlphaValueName, MaterialModel->GetGlobalParameterValue(UDynamicMaterialModel::GlobalSurfaceThicknessValueName));
}

void UDynamicMaterialModelEditorOnlyData::OnDomainChanged()
{
	switch (Domain)
	{
		case EMaterialDomain::MD_PostProcess:
		case EMaterialDomain::MD_LightFunction:
		{
			const FDMUpdateGuard Guard;

			// Post process only supports emissive.
			UDMMaterialSlot* BaseColorSlot = GetSlotForMaterialProperty(EDMMaterialPropertyType::BaseColor);
			UDMMaterialSlot* EmissiveSlot = GetSlotForMaterialProperty(EDMMaterialPropertyType::EmissiveColor);

			if (!EmissiveSlot)
			{
				if (BaseColorSlot)
				{
					EnsureSwapSlotMaterialProperty(EDMMaterialPropertyType::BaseColor, EDMMaterialPropertyType::EmissiveColor);
				}
				else
				{
					AddSlotForMaterialProperty(EDMMaterialPropertyType::EmissiveColor);
				}
			}

			SetShadingModel(EDMMaterialShadingModel::Unlit);
			SetBlendMode(EBlendMode::BLEND_Opaque);
			break;
		}

		case EMaterialDomain::MD_DeferredDecal:
			SetShadingModel(EDMMaterialShadingModel::DefaultLit);
			SetBlendMode(EBlendMode::BLEND_Translucent);
			break;
	}

	RequestMaterialBuild();
}

void UDynamicMaterialModelEditorOnlyData::OnBlendModeChanged()
{
	switch (BlendMode)
	{
		case EBlendMode::BLEND_Opaque:
			SetHasPixelAnimation(false);
			RemoveSlotForMaterialProperty(EDMMaterialPropertyType::Opacity);
			RemoveSlotForMaterialProperty(EDMMaterialPropertyType::OpacityMask);
			break;

		case EBlendMode::BLEND_Masked:
			SetHasPixelAnimation(false);
			EnsureSwapSlotMaterialProperty(EDMMaterialPropertyType::Opacity, EDMMaterialPropertyType::OpacityMask);
			break;

		case EBlendMode::BLEND_Translucent:
		case EBlendMode::BLEND_Additive:
		case EBlendMode::BLEND_Modulate:
			EnsureSwapSlotMaterialProperty(EDMMaterialPropertyType::OpacityMask, EDMMaterialPropertyType::Opacity);
			break;
	}

	RequestMaterialBuild();
}

void UDynamicMaterialModelEditorOnlyData::OnShadingModelChanged()
{
	RequestMaterialBuild();
}

void UDynamicMaterialModelEditorOnlyData::OnMaterialFlagChanged()
{
	RequestMaterialBuild();
}

void UDynamicMaterialModelEditorOnlyData::Initialize()
{
	if (!Slots.IsEmpty())
	{
		return;
	}

	AssignPropertyAlphaValues();
}

UMaterial* UDynamicMaterialModelEditorOnlyData::GetGeneratedMaterial() const
{
	return IsValid(MaterialModel) ? MaterialModel->DynamicMaterial : nullptr;
}

void UDynamicMaterialModelEditorOnlyData::CreateMaterial()
{
	if (!IsValid(MaterialModel))
	{
		return;
	}

	if (FDynamicMaterialModule::IsMaterialExportEnabled() == false)
	{
		UMaterialFactoryNew* MaterialFactory = NewObject<UMaterialFactoryNew>();
		check(MaterialFactory);

		MaterialModel->DynamicMaterial = Cast<UMaterial>(MaterialFactory->FactoryCreateNew(
			UMaterial::StaticClass(),
			MaterialModel,
			NAME_None,
			RF_DuplicateTransient | RF_TextExportTransient | RF_Public,
			nullptr,
			GWarn
		));
	}
	else
	{
		FString MaterialBaseName = GetName() + "-" + FGuid::NewGuid().ToString();
		const FString FullName = "/Game/DynamicMaterials/" + MaterialBaseName;
		UPackage* Package = CreatePackage(*FullName);

		UMaterialFactoryNew* MaterialFactory = NewObject<UMaterialFactoryNew>();
		check(MaterialFactory);

		MaterialModel->DynamicMaterial = Cast<UMaterial>(MaterialFactory->FactoryCreateNew(
			UMaterial::StaticClass(),
			Package,
			*MaterialBaseName,
			RF_DuplicateTransient | RF_TextExportTransient | RF_Standalone | RF_Public,
			nullptr,
			GWarn
		));

		FAssetRegistryModule::AssetCreated(MaterialModel->DynamicMaterial);
	}
}

void UDynamicMaterialModelEditorOnlyData::BuildMaterial(bool bInDirtyAssets)
{
	if (State != EDMState::Idle)
	{
		checkNoEntry();
		return;
	}

	if (!IsValid(MaterialModel))
	{
		return;
	}

	UE_LOG(LogDynamicMaterialEditor, Display, TEXT("Building Material Designer Material (%s)..."), *MaterialModel->GetFullName());

	if (!IsValid(MaterialModel->DynamicMaterial))
	{
		CreateMaterial();
	}

	State = EDMState::Building;
	Expressions.Empty();
	MaterialModel->DynamicMaterial->MaterialDomain = Domain;
	MaterialModel->DynamicMaterial->BlendMode = BlendMode;
	MaterialModel->DynamicMaterial->bHasPixelAnimation = bHasPixelAnimation;
	MaterialModel->DynamicMaterial->TwoSided = bTwoSided;
	MaterialModel->DynamicMaterial->bOutputTranslucentVelocity = bOutputTranslucentVelocityEnabled;
	MaterialModel->DynamicMaterial->bEnableTessellation = bNaniteTessellationEnabled;
	MaterialModel->DynamicMaterial->bEnableResponsiveAA = bResponsiveAAEnabled;

	// Not setting this to true can cause the level associated with this material to dirty itself when it
	// is used with Niagara. It doesn't negatively affect the material in any meaningful way.
	MaterialModel->DynamicMaterial->bUsedWithNiagaraMeshParticles = true;

	// If bNaniteTessellationEnabled is false, bUsedWithNanite will be set back to true if there's a displacement property.
	MaterialModel->DynamicMaterial->bUsedWithNanite = bNaniteTessellationEnabled;

	switch (ShadingModel)
	{
		case EDMMaterialShadingModel::DefaultLit:
			MaterialModel->DynamicMaterial->SetShadingModel(EMaterialShadingModel::MSM_DefaultLit);
			break;

		case EDMMaterialShadingModel::Unlit:
			MaterialModel->DynamicMaterial->SetShadingModel(EMaterialShadingModel::MSM_Unlit);
			break;

		default:
			checkNoEntry();
			break;
	}

	TSharedPtr<FDMMaterialBuildState> BuildState = CreateBuildState(MaterialModel->DynamicMaterial, bInDirtyAssets);

	/**
	 * Process slots to build base material inputs.
	 */
	UE::DynamicMaterial::ForEachMaterialPropertyType(
		[this, &BuildState](EDMMaterialPropertyType InType)
		{
			if (FExpressionInput* Input = BuildState->GetMaterialProperty(InType))
			{
				Input->Expression = nullptr;
			}

			if (UDMMaterialProperty* Property = GetMaterialProperty(InType))
			{
				if (Property->IsEnabled() && Property->IsMaterialPin() && GetSlotForMaterialProperty(InType))
				{
					Property->GenerateExpressions(BuildState.ToSharedRef());

					// Global opacity is handled at later
					if (InType != EDMMaterialPropertyType::Opacity && InType != EDMMaterialPropertyType::OpacityMask)
					{
						Property->AddAlphaMultiplier(BuildState.ToSharedRef());
					}
				}
			}

			return EDMIterationResult::Continue;
		}
	);

	if (Domain != EMaterialDomain::MD_PostProcess && Domain != EMaterialDomain::MD_LightFunction)
	{
		/**
		 * Generate opacity input based on base/emissive if it doesn't already have an input.
		 */
		EDMMaterialPropertyType GenerateOpacityInput = EDMMaterialPropertyType::None;

		// Masked materials use the mask property.
		if (BlendMode == EBlendMode::BLEND_Masked)
		{
			if (GetSlotForEnabledMaterialProperty(EDMMaterialPropertyType::OpacityMask) == nullptr)
			{
				GenerateOpacityInput = EDMMaterialPropertyType::OpacityMask;
			}
		}
		// Any other translucent material will use the opacity property.
		else if (BlendMode != EBlendMode::BLEND_Opaque)
		{
			if (GetSlotForEnabledMaterialProperty(EDMMaterialPropertyType::Opacity) == nullptr)
			{
				GenerateOpacityInput = EDMMaterialPropertyType::Opacity;
			}
		}

		if (GenerateOpacityInput != EDMMaterialPropertyType::None)
		{
			UDMMaterialSlot* OpacitySlot = nullptr;
			EDMMaterialPropertyType OpacityProperty = EDMMaterialPropertyType::None;

			if (UDMMaterialSlot* BaseColorSlot = GetSlotForEnabledMaterialProperty(EDMMaterialPropertyType::BaseColor))
			{
				OpacitySlot = BaseColorSlot;
				OpacityProperty = EDMMaterialPropertyType::BaseColor;
			}
			else if (UDMMaterialSlot* EmissiveSlot = GetSlotForEnabledMaterialProperty(EDMMaterialPropertyType::EmissiveColor))
			{
				OpacitySlot = EmissiveSlot;
				OpacityProperty = EDMMaterialPropertyType::EmissiveColor;
			}

			if (OpacitySlot)
			{
				UMaterialExpression* OpacityOutputNode;
				int32 OutputIndex;
				int32 OutputChannel;
				UDMMaterialProperty::GenerateOpacityExpressions(BuildState.ToSharedRef(), OpacitySlot, OpacityProperty, OpacityOutputNode, OutputIndex, OutputChannel);

				if (OpacityOutputNode)
				{
					if (FExpressionInput* OpacityPropertyPtr = BuildState->GetMaterialProperty(GenerateOpacityInput))
					{
						OpacityPropertyPtr->Expression = OpacityOutputNode;
						OpacityPropertyPtr->OutputIndex = 0;
						OpacityPropertyPtr->Mask = 0;

						if (OutputChannel != FDMMaterialStageConnectorChannel::WHOLE_CHANNEL)
						{
							OpacityPropertyPtr->Mask = 1;
							OpacityPropertyPtr->MaskR = !!(OutputChannel & FDMMaterialStageConnectorChannel::FIRST_CHANNEL);
							OpacityPropertyPtr->MaskG = !!(OutputChannel & FDMMaterialStageConnectorChannel::SECOND_CHANNEL);
							OpacityPropertyPtr->MaskB = !!(OutputChannel & FDMMaterialStageConnectorChannel::THIRD_CHANNEL);
							OpacityPropertyPtr->MaskA = !!(OutputChannel & FDMMaterialStageConnectorChannel::FOURTH_CHANNEL);
						}
					}
				}
			}
		}

		/**
		 * Apply global opacity slider after automatic opacity generation
		 */
		UDMMaterialProperty* OpacityProperty = nullptr;

		if (BlendMode == BLEND_Masked)
		{
			OpacityProperty = GetMaterialProperty(EDMMaterialPropertyType::OpacityMask);
		}
		else if (BlendMode != BLEND_Opaque)
		{
			OpacityProperty = GetMaterialProperty(EDMMaterialPropertyType::Opacity);
		}

		if (OpacityProperty != nullptr && OpacityProperty->IsEnabled())
		{
			OpacityProperty->AddAlphaMultiplier(BuildState.ToSharedRef());
		}
	}

	/**
	 * Apply output processors.
	 */
	UE::DynamicMaterial::ForEachMaterialPropertyType(
		[this, &BuildState](EDMMaterialPropertyType InType)
		{
			if (UDMMaterialProperty* Property = GetMaterialProperty(InType))
			{
				if (Property->IsMaterialPin() && Property->IsEnabled() && GetSlotForMaterialProperty(InType))
				{
					Property->AddOutputProcessor(BuildState.ToSharedRef());
				}
			}

			return EDMIterationResult::Continue;
		}
	);

	if (IsValid(MaterialModel) && IsValid(MaterialModel->DynamicMaterialInstance))
	{
		MaterialModel->DynamicMaterialInstance->OnMaterialBuilt(MaterialModel);
	}

	/**
	 * To generate the statistics, you need to force a material recompile. The build state object does this in its destructor.
	 * Resetting the build state SharedPtr destroys the object and thus generates the material shaders.
	 */
	BuildState.Reset();

	/**
	 * GetStatistics can call the GC which could potentially delete the material under us.
	 * Add a reference to it while getting statistics to prevent it being destroyed.
	 */
	{
		TStrongObjectPtr<UObject> ScopeReference(MaterialModel->DynamicMaterial);
		MaterialStats = UMaterialEditingLibrary::GetStatistics(MaterialModel->DynamicMaterial);
	}

	State = EDMState::Idle;

	OnMaterialBuiltDelegate.Broadcast(MaterialModel);
}

void UDynamicMaterialModelEditorOnlyData::RequestMaterialBuild(bool bInImmediate)
{
	if (!HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
	{
		if (bInImmediate)
		{
			BuildMaterial(/* Dirty Assets */ false);
		}
		else if (UDMBuildRequestSubsystem* BuildRequestSubsystem = UDMBuildRequestSubsystem::Get())
		{
			BuildRequestSubsystem->AddBuildRequest(this, /* Dirty Packages */ !UE::GetIsEditorLoadingPackage());
		}
	}
}

void UDynamicMaterialModelEditorOnlyData::OnValueListUpdate()
{
	OnValueListUpdateDelegate.Broadcast(MaterialModel);
}

void UDynamicMaterialModelEditorOnlyData::OnPropertyUpdate(UDMMaterialProperty* InProperty)
{
	RequestMaterialBuild();

	OnPropertyUpdateDelegate.Broadcast(MaterialModel);
}

TSharedRef<FDMMaterialBuildState> UDynamicMaterialModelEditorOnlyData::CreateBuildState(UMaterial* InMaterialToBuild, bool bInDirtyAssets) const
{
	check(MaterialModel);
	check(InMaterialToBuild);

	TSharedRef<FDMMaterialBuildState> BuildState = MakeShared<FDMMaterialBuildState>(InMaterialToBuild, MaterialModel, bInDirtyAssets);

	/**
	 * Add global UV parameters
	 */
	if (UDMMaterialValue* GlobalOffsetValue = MaterialModel->GetGlobalParameterValue(UDynamicMaterialModel::GlobalOffsetValueName))
	{
		GlobalOffsetValue->GenerateExpression(BuildState);
		BuildState->SetGlobalExpression(UDynamicMaterialModel::GlobalOffsetValueName, BuildState->GetLastValueExpression(GlobalOffsetValue));
	}

	if (UDMMaterialValue* GlobalTilingValue = MaterialModel->GetGlobalParameterValue(UDynamicMaterialModel::GlobalTilingValueName))
	{
		GlobalTilingValue->GenerateExpression(BuildState);
		BuildState->SetGlobalExpression(UDynamicMaterialModel::GlobalTilingValueName, BuildState->GetLastValueExpression(GlobalTilingValue));
	}

	if (UDMMaterialValue* GlobalRotationValue = MaterialModel->GetGlobalParameterValue(UDynamicMaterialModel::GlobalRotationValueName))
	{
		GlobalRotationValue->GenerateExpression(BuildState);
		BuildState->SetGlobalExpression(UDynamicMaterialModel::GlobalRotationValueName, BuildState->GetLastValueExpression(GlobalRotationValue));
	}

	return BuildState;
}

bool UDynamicMaterialModelEditorOnlyData::AddTextureSet(UDMTextureSet* InTextureSet, bool bInReplaceSlots)
{
	bool bMadeChange = false;

	for (const TPair<EDMTextureSetMaterialProperty, FDMMaterialTexture>& MaterialTexture : InTextureSet->GetTextures())
	{
		if (MaterialTexture.Value.Texture.IsNull())
		{
			continue;
		}

		const EDMMaterialPropertyType PropertyType = FDMUtils::TextureSetMaterialPropertyToMaterialPropertyType(MaterialTexture.Key);

		if (PropertyType == EDMMaterialPropertyType::None)
		{
			continue;
		}

		UDMMaterialProperty* Property = GetMaterialProperty(PropertyType);

		if (!Property)
		{
			continue;
		}

		if (!Property->IsEnabled())
		{
			Property->SetEnabled(true);
		}

		UDMMaterialSlot* Slot = GetSlotForMaterialProperty(PropertyType);

		if (!Slot)
		{
			Slot = AddSlotForMaterialProperty(PropertyType);

			if (!Slot)
			{
				continue;
			}
		}

		UTexture* Texture = MaterialTexture.Value.Texture.LoadSynchronous();

		if (!Texture)
		{
			continue;
		}

		if (GUndo)
		{
			Slot->Modify();
		}

		UDMMaterialLayerObject* Layer = nullptr;

		{
			const FDMUpdateGuard Guard;

			Layer = Slot->AddDefaultLayer(PropertyType);

			if (!ensure(Layer))
			{
				continue;
			}

			bMadeChange = true;

			UDMMaterialStage* Stage = Layer->GetStage(EDMMaterialLayerStage::Base);

			if (!ensure(Stage))
			{
				continue;
			}

			UDMMaterialStageInputExpression* NewExpression = UDMMaterialStageInputExpression::ChangeStageInput_Expression(
				Stage,
				UDMMaterialStageExpressionTextureSample::StaticClass(),
				UDMMaterialStageBlend::InputB,
				FDMMaterialStageConnectorChannel::WHOLE_CHANNEL,
				0,
				FDMMaterialStageConnectorChannel::WHOLE_CHANNEL
			);

			if (!ensure(NewExpression))
			{
				continue;
			}

			UDMMaterialSubStage* SubStage = NewExpression->GetSubStage();

			if (!ensure(SubStage))
			{
				continue;
			}

			UDMMaterialStageInputValue* InputValue = UDMMaterialStageInputValue::ChangeStageInput_NewLocalValue(
				SubStage,
				0,
				FDMMaterialStageConnectorChannel::WHOLE_CHANNEL,
				EDMValueType::VT_Texture,
				FDMMaterialStageConnectorChannel::WHOLE_CHANNEL
			);

			if (ensure(InputValue))
			{
				UDMMaterialValueTexture* InputTexture = Cast<UDMMaterialValueTexture>(InputValue->GetValue());

				if (ensure(InputTexture))
				{

					InputTexture->SetValue(Texture);
				}
			}

			if (UDMMaterialStageBlend* Blend = Cast<UDMMaterialStageBlend>(Stage->GetSource()))
			{
				EAvaColorChannel AvaColorChannel = EAvaColorChannel::None;

				if (EnumHasAnyFlags(MaterialTexture.Value.TextureChannel, EDMTextureChannelMask::Red))
				{
					AvaColorChannel |= EAvaColorChannel::Red;
				}

				if (EnumHasAnyFlags(MaterialTexture.Value.TextureChannel, EDMTextureChannelMask::Green))
				{
					AvaColorChannel |= EAvaColorChannel::Green;
				}

				if (EnumHasAnyFlags(MaterialTexture.Value.TextureChannel, EDMTextureChannelMask::Blue))
				{
					AvaColorChannel |= EAvaColorChannel::Blue;
				}

				if (EnumHasAnyFlags(MaterialTexture.Value.TextureChannel, EDMTextureChannelMask::Alpha))
				{
					AvaColorChannel |= EAvaColorChannel::Alpha;
				}

				if (AvaColorChannel != EAvaColorChannel::RGBA)
				{
					Blend->SetBaseChannelOverride(AvaColorChannel);
				}				
			}

			if (bInReplaceSlots)
			{
				for (int32 Index = Slot->GetLayers().Num() - 1; Index >= 0; --Index)
				{
					UDMMaterialLayerObject* LayerIter = Slot->GetLayer(Index);

					if (!LayerIter || LayerIter->GetStage(EDMMaterialLayerStage::Base) == Stage)
					{
						continue;
					}

					Slot->RemoveLayer(LayerIter);
				}
			}
		}

		Layer->Update(Layer, EDMUpdateType::Structure);
	}

	return bMadeChange;
}

bool UDynamicMaterialModelEditorOnlyData::NeedsWizard() const
{
	return PropertySlotMap.IsEmpty();
}

void UDynamicMaterialModelEditorOnlyData::OnWizardComplete()
{
	if (UDynamicMaterialModel* MaterialModelLocal = MaterialModel.Get())
	{
		FDynamicMaterialEditorModule::Get().OnWizardComplete(MaterialModelLocal);
	}
}

void UDynamicMaterialModelEditorOnlyData::SetChannelListPreset(FName InPresetName)
{
	const FDMMaterialChannelListPreset* Preset = GetDefault<UDynamicMaterialEditorSettings>()->GetPresetByName(InPresetName);

	if (!Preset)
	{
		return;
	}

	UE::DynamicMaterial::ForEachMaterialPropertyType(
		[this, Preset](EDMMaterialPropertyType InProperty)
		{
			if (InProperty == EDMMaterialPropertyType::OpacityMask)
			{
				return EDMIterationResult::Continue;
			}

			if (Preset->IsPropertyEnabled(InProperty))
			{
				AddSlotForMaterialProperty(InProperty);
			}
			else
			{
				RemoveSlotForMaterialProperty(InProperty);
			}

			return EDMIterationResult::Continue;
		});

	SetBlendMode(Preset->DefaultBlendMode);
	SetShadingModel(Preset->DefaultShadingModel);
	SetHasPixelAnimation(Preset->bDefaultAnimated);
	SetIsTwoSided(Preset->bDefaultTwoSided);
}

const FMaterialStatistics& UDynamicMaterialModelEditorOnlyData::GetMaterialStats() const
{
	return MaterialStats;
}

UDMMaterialComponent* UDynamicMaterialModelEditorOnlyData::GetSubComponentByPath(FDMComponentPath& InPath,
	const FDMComponentPathSegment& InPathSegment) const
{
	if (InPathSegment.GetToken() == SlotsPathToken)
	{
		int32 SlotIndex;

		if (InPathSegment.GetParameter(SlotIndex))
		{
			if (Slots.IsValidIndex(SlotIndex))
			{
				return Slots[SlotIndex]->GetComponentByPath(InPath);
			}
		}

		return nullptr;
	}

	if (InPathSegment.GetToken() == PropertiesPathToken)
	{
		FString PropertyStr;

		if (InPathSegment.GetParameter(PropertyStr))
		{
			UEnum* PropertyEnum = StaticEnum<EDMMaterialPropertyType>();
			int64 IntValue = PropertyEnum->GetValueByNameString(PropertyStr);

			if (IntValue != INDEX_NONE)
			{
				EDMMaterialPropertyType EnumValue = static_cast<EDMMaterialPropertyType>(IntValue);

				if (UDMMaterialProperty* PropertyPtr = GetMaterialProperty(EnumValue))
				{
					return PropertyPtr->GetComponentByPath(InPath);
				}
			}
		}
	}

	// Channels
	using namespace UE::DynamicMaterialEditor::Private;

	if (const EDMMaterialPropertyType* PropertyPtr = TokenToPropertyMap.Find(FString(InPathSegment.GetToken())))
	{
		if (const TObjectPtr<UDMMaterialSlot>* SlotPtr = PropertySlotMap.Find(*PropertyPtr))
		{
			return (*SlotPtr)->GetComponentByPath(InPath);
		}
	}

	return nullptr;
}

TSharedRef<IDMMaterialBuildStateInterface> UDynamicMaterialModelEditorOnlyData::CreateBuildStateInterface(UMaterial* InMaterialToBuild) const
{
	return CreateBuildState(InMaterialToBuild);
}

void UDynamicMaterialModelEditorOnlyData::SetPropertyComponent(EDMMaterialPropertyType InPropertyType, FName InComponentName, UDMMaterialComponent* InComponent)
{
	if (UDMMaterialProperty* Property = GetMaterialProperty(InPropertyType))
	{
		Property->AddComponent(InComponentName, InComponent);
	}
}

UDMMaterialComponent* UDynamicMaterialModelEditorOnlyData::GetSubComponentByPath(FDMComponentPath& InPath) const
{
	if (InPath.IsLeaf())
	{
		// This is not a component
		return nullptr;
	}

	// Fetches the first component of the path and removes it from the path
	const FDMComponentPathSegment FirstComponent = InPath.GetFirstSegment();

	if (UDMMaterialComponent* SubComponent = GetSubComponentByPath(InPath, FirstComponent))
	{
		return SubComponent->GetComponentByPath(InPath);
	}

	return nullptr;
}

void UDynamicMaterialModelEditorOnlyData::DoBuild_Implementation(bool bInDirtyAssets)
{
	BuildMaterial(bInDirtyAssets);
}

void UDynamicMaterialModelEditorOnlyData::SwapSlotMaterialProperty(EDMMaterialPropertyType InPropertyFrom, EDMMaterialPropertyType InPropertyTo)
{
	UDMMaterialSlot* FromSlot = GetSlotForMaterialProperty(InPropertyFrom);
	
	if (!FromSlot)
	{
		return;
	}

	if (UDMMaterialSlot* ToSlot = GetSlotForMaterialProperty(InPropertyTo))
	{
		if (ToSlot == FromSlot)
		{
			return;
		}

		RemoveSlotForMaterialProperty(InPropertyFrom);
	}

	FromSlot->ChangeMaterialProperty(InPropertyFrom, InPropertyTo);
}

void UDynamicMaterialModelEditorOnlyData::EnsureSwapSlotMaterialProperty(EDMMaterialPropertyType InPropertyFrom, EDMMaterialPropertyType InPropertyTo)
{
	if (UDMMaterialSlot* ToSlot = GetSlotForMaterialProperty(InPropertyTo))
	{
		if (UDMMaterialSlot* FromSlot = GetSlotForMaterialProperty(InPropertyFrom))
		{
			if (ToSlot != FromSlot)
			{
				RemoveSlotForMaterialProperty(InPropertyFrom);
			}
		}
	}
	else if (GetSlotForMaterialProperty(InPropertyFrom))
	{
		SwapSlotMaterialProperty(InPropertyFrom, InPropertyTo);
	}
	else
	{
		AddSlotForMaterialProperty(InPropertyTo);
	}
}

void UDynamicMaterialModelEditorOnlyData::SetDomain(TEnumAsByte<EMaterialDomain> InDomain)
{
	if (Domain == InDomain)
	{
		return;
	}

	Domain = InDomain;

	OnDomainChanged();
}

void UDynamicMaterialModelEditorOnlyData::SetBlendMode(TEnumAsByte<EBlendMode> InBlendMode)
{
	if (BlendMode == InBlendMode)
	{
		return;
	}

	BlendMode = InBlendMode;

	OnBlendModeChanged();
}

void UDynamicMaterialModelEditorOnlyData::SetShadingModel(EDMMaterialShadingModel InShadingModel)
{
	if (ShadingModel == InShadingModel)
	{
		return;
	}

	ShadingModel = InShadingModel;

	OnShadingModelChanged();
}

bool UDynamicMaterialModelEditorOnlyData::GetHasPixelAnimation() const
{
	return bHasPixelAnimation;
}

void UDynamicMaterialModelEditorOnlyData::SetHasPixelAnimation(bool bInHasAnimation)
{
	if (bHasPixelAnimation == bInHasAnimation)
	{
		return;
	}

	bHasPixelAnimation = bInHasAnimation;

	OnMaterialFlagChanged();
}

bool UDynamicMaterialModelEditorOnlyData::GetIsTwoSided() const
{
	return bTwoSided;
}

void UDynamicMaterialModelEditorOnlyData::SetIsTwoSided(bool bInEnabled)
{
	if (bTwoSided == bInEnabled)
	{
		return;
	}

	bTwoSided = bInEnabled;

	OnMaterialFlagChanged();
}

bool UDynamicMaterialModelEditorOnlyData::IsOutputTranslucentVelocityEnabled() const
{
	return bOutputTranslucentVelocityEnabled;
}

void UDynamicMaterialModelEditorOnlyData::SetOutputTranslucentVelocityEnabled(bool bInEnabled)
{
	if (bOutputTranslucentVelocityEnabled == bInEnabled)
	{
		return;
	}

	bOutputTranslucentVelocityEnabled = bInEnabled;

	OnMaterialFlagChanged();
}

bool UDynamicMaterialModelEditorOnlyData::IsNaniteTessellationEnabled() const
{
	return bNaniteTessellationEnabled;
}

void UDynamicMaterialModelEditorOnlyData::SetNaniteTessellationEnabled(bool bInEnabled)
{
	if (bNaniteTessellationEnabled == bInEnabled)
	{
		return;
	}

	bNaniteTessellationEnabled = bInEnabled;

	OnMaterialFlagChanged();
}

bool UDynamicMaterialModelEditorOnlyData::IsResponsiveAAEnabled() const
{
	return bResponsiveAAEnabled;
}

void UDynamicMaterialModelEditorOnlyData::SetResponsiveAAEnabled(bool bInEnabled)
{
	if (bResponsiveAAEnabled == bInEnabled)
	{
		return;
	}

	bResponsiveAAEnabled = bInEnabled;

	OnMaterialFlagChanged();
}

void UDynamicMaterialModelEditorOnlyData::OpenMaterialEditor() const
{
	if (!IsValid(MaterialModel) || !IsValid(MaterialModel->DynamicMaterial))
	{
		return;
	}

	IAssetTools& AssetTools = FAssetToolsModule::GetModule().Get();
	AssetTools.OpenEditorForAssets({MaterialModel->DynamicMaterial});
}

TMap<EDMMaterialPropertyType, UDMMaterialProperty*> UDynamicMaterialModelEditorOnlyData::GetMaterialProperties() const
{
	TMap<EDMMaterialPropertyType, UDMMaterialProperty*> LocalProperties;

	UE::DynamicMaterial::ForEachMaterialPropertyType(
		[this, &LocalProperties](EDMMaterialPropertyType InType)
		{
			if (UDMMaterialProperty* Property = GetMaterialProperty(InType))
			{
				LocalProperties.Emplace(InType, Property);
			}

			return EDMIterationResult::Continue;
		}
	);

	return LocalProperties;
}

UDMMaterialProperty* UDynamicMaterialModelEditorOnlyData::GetMaterialProperty(EDMMaterialPropertyType InMaterialProperty) const
{
	switch (InMaterialProperty)
	{
		case EDMMaterialPropertyType::BaseColor:
			return BaseColor;

		case EDMMaterialPropertyType::EmissiveColor:
			return EmissiveColor;

		case EDMMaterialPropertyType::Opacity:
			return Opacity;

		case EDMMaterialPropertyType::OpacityMask:
			return OpacityMask;

		case EDMMaterialPropertyType::Roughness:
			return Roughness;

		case EDMMaterialPropertyType::Specular:
			return Specular;

		case EDMMaterialPropertyType::Metallic:
			return Metallic;

		case EDMMaterialPropertyType::Normal:
			return Normal;

		case EDMMaterialPropertyType::PixelDepthOffset:
			return PixelDepthOffset;

		case EDMMaterialPropertyType::WorldPositionOffset:
			return WorldPositionOffset;

		case EDMMaterialPropertyType::AmbientOcclusion:
			return AmbientOcclusion;

		case EDMMaterialPropertyType::Anisotropy:
			return Anisotropy;

		case EDMMaterialPropertyType::Refraction:
			return Refraction;

		case EDMMaterialPropertyType::Tangent:
			return Tangent;

		case EDMMaterialPropertyType::Displacement:
			return Displacement;

		case EDMMaterialPropertyType::SubsurfaceColor:
			return SubsurfaceColor;

		case EDMMaterialPropertyType::SurfaceThickness:
			return SurfaceThickness;

		case EDMMaterialPropertyType::Custom1:
			return Custom1;

		case EDMMaterialPropertyType::Custom2:
			return Custom2;

		case EDMMaterialPropertyType::Custom3:
			return Custom3;

		case EDMMaterialPropertyType::Custom4:
			return Custom4;

		default:
			return nullptr;
	}	
}

UDMMaterialSlot* UDynamicMaterialModelEditorOnlyData::GetSlot(int32 Index) const
{
	if (Slots.IsValidIndex(Index))
	{
		return Slots[Index];		
	}

	return nullptr;
}

UDMMaterialSlot* UDynamicMaterialModelEditorOnlyData::GetSlotForMaterialProperty(EDMMaterialPropertyType InType) const
{
	if (const TObjectPtr<UDMMaterialSlot>* SlotPtr = PropertySlotMap.Find(InType))
	{
		return *SlotPtr;
	}

	return nullptr;
}

UDMMaterialSlot* UDynamicMaterialModelEditorOnlyData::GetSlotForEnabledMaterialProperty(EDMMaterialPropertyType InType) const
{
	UDMMaterialProperty* Property = GetMaterialProperty(InType);

	if (!Property || !Property->IsEnabled())
	{
		return nullptr;
	}

	return GetSlotForMaterialProperty(InType);
}

UDMMaterialSlot* UDynamicMaterialModelEditorOnlyData::AddSlot()
{
	UDMMaterialSlot* NewSlot = nullptr;

	UE::DynamicMaterial::ForEachMaterialPropertyType(
		[this, &NewSlot](EDMMaterialPropertyType InProperty)
		{
			if (GetSlotForMaterialProperty(InProperty))
			{
				return EDMIterationResult::Continue;
			}

			NewSlot = AddSlotForMaterialProperty(InProperty);
			return EDMIterationResult::Break;
		});

	return NewSlot;
}

UDMMaterialSlot* UDynamicMaterialModelEditorOnlyData::AddSlotForMaterialProperty(EDMMaterialPropertyType InType)
{
	if (InType != EDMMaterialPropertyType::EmissiveColor 
		&& (Domain == EMaterialDomain::MD_PostProcess || Domain == EMaterialDomain::MD_LightFunction))
	{
		return nullptr;
	}
	if (InType == EDMMaterialPropertyType::Opacity || InType == EDMMaterialPropertyType::OpacityMask)
	{
		switch (BlendMode)
		{
			case EBlendMode::BLEND_Translucent:
			case EBlendMode::BLEND_Additive:
			case EBlendMode::BLEND_Modulate:
				InType = EDMMaterialPropertyType::Opacity;
				break;

			case EBlendMode::BLEND_Masked:
				InType = EDMMaterialPropertyType::OpacityMask;
				break;

			case EBlendMode::BLEND_Opaque:
				return nullptr;

			default:
				checkNoEntry();
				break;
		}
	}

	if (UDMMaterialSlot* ExistingSlot = GetSlotForMaterialProperty(InType))
	{
		return ExistingSlot;
	}

	// BaseColor and Emissive are mutually exclusive so if something tries to add one of them, the other must
	// be checked. If it is found, it is converted and returned. The same goes for Opacity and OpacityMask.
	switch (InType)
	{
		case EDMMaterialPropertyType::Opacity:
			if (UDMMaterialSlot* ExistingSlot = GetSlotForMaterialProperty(EDMMaterialPropertyType::OpacityMask))
			{
				SwapSlotMaterialProperty(EDMMaterialPropertyType::OpacityMask, EDMMaterialPropertyType::Opacity);
				return ExistingSlot;
			}
			break;

		case EDMMaterialPropertyType::OpacityMask:
			if (UDMMaterialSlot* ExistingSlot = GetSlotForMaterialProperty(EDMMaterialPropertyType::Opacity))
			{
				SwapSlotMaterialProperty(EDMMaterialPropertyType::Opacity, EDMMaterialPropertyType::OpacityMask);
				return ExistingSlot;
			}
			break;
	}

	UDMMaterialSlot* NewSlot = NewObject<UDMMaterialSlot>(this, NAME_None, RF_Transactional);
	check(NewSlot);

	AssignMaterialPropertyToSlot(InType, NewSlot);

	NewSlot->SetIndex(Slots.Num());
	Slots.Add(NewSlot);
	NewSlot->SetComponentState(EDMComponentLifetimeState::Added);

	NewSlot->GetOnConnectorsUpdateDelegate().AddUObject(this, &UDynamicMaterialModelEditorOnlyData::OnSlotConnectorsUpdated);

	if (UDMMaterialProperty* Property = GetMaterialProperty(InType))
	{
		Property->OnSlotAdded(NewSlot);
	}

	NewSlot->Update(NewSlot, EDMUpdateType::Structure);

	OnSlotListUpdateDelegate.Broadcast(MaterialModel);

	RequestMaterialBuild();

	return NewSlot;
}

UDMMaterialSlot* UDynamicMaterialModelEditorOnlyData::RemoveSlot(int32 Index)
{
	UDMMaterialSlot* Slot = GetSlot(Index);
	
	if (!Slot)
	{
		return nullptr;
	}
	
	if (GUndo)
	{
		Slot->Modify(/* Always mark dirty */ false);
	}

	for (EDMMaterialPropertyType MaterialProperty : GetMaterialPropertiesForSlot(Slot))
	{
		UnassignMaterialProperty(MaterialProperty);
	}

	const EDMMaterialPropertyType* Key = PropertySlotMap.FindKey(Slot);

	if (Key)
	{
		PropertySlotMap.Remove(*Key);
	}

	const int32 SlotIndex = Slots.IndexOfByKey(Slot);

	if (SlotIndex != INDEX_NONE)
	{
		Slots.RemoveAtSwap(SlotIndex);
		
		if (Slots.IsValidIndex(SlotIndex))
		{
			if (GUndo)
			{
				Slots[SlotIndex]->Modify(/* Always mark dirty */ false);
			}

			Slots[SlotIndex]->SetIndex(SlotIndex);
		}
	}

	Slot->GetOnConnectorsUpdateDelegate().RemoveAll(this);
	Slot->SetComponentState(EDMComponentLifetimeState::Removed);

	RequestMaterialBuild();

	OnSlotListUpdateDelegate.Broadcast(MaterialModel);
	
	return Slot;
}

UDMMaterialSlot* UDynamicMaterialModelEditorOnlyData::RemoveSlotForMaterialProperty(EDMMaterialPropertyType InType)
{
	if (const TObjectPtr<UDMMaterialSlot>* SlotPtr = PropertySlotMap.Find(InType))
	{
		return RemoveSlot(Slots.IndexOfByKey(*SlotPtr));
	}

	return nullptr;
}

TArray<EDMMaterialPropertyType> UDynamicMaterialModelEditorOnlyData::GetMaterialPropertiesForSlot(const UDMMaterialSlot* InSlot) const
{
	TArray<EDMMaterialPropertyType> OutProperties;

	for (const TPair<EDMMaterialPropertyType, TObjectPtr<UDMMaterialSlot>>& Pair : PropertySlotMap)
	{
		if (Pair.Value == InSlot)
		{
			OutProperties.Add(Pair.Key);
		}
	}

	return OutProperties;
}

void UDynamicMaterialModelEditorOnlyData::AssignMaterialPropertyToSlot(EDMMaterialPropertyType InProperty, UDMMaterialSlot* InSlot)
{
	if (!InSlot)
	{
		UnassignMaterialProperty(InProperty);
		return;
	}

	UDMMaterialProperty* Property = GetMaterialProperty(InProperty);
	check(Property);

	PropertySlotMap.FindOrAdd(InProperty) = InSlot;
	Property->ResetInputConnectionMap();
	InSlot->OnPropertiesUpdated();

	RequestMaterialBuild();
}

void UDynamicMaterialModelEditorOnlyData::UnassignMaterialProperty(EDMMaterialPropertyType InProperty)
{
	TObjectPtr<UDMMaterialSlot>* SlotPtr = PropertySlotMap.Find(InProperty);
	
	if (!SlotPtr)
	{
		return;
	}

	PropertySlotMap.Remove(InProperty);
	(*SlotPtr)->OnPropertiesUpdated();

	RequestMaterialBuild();
}

void UDynamicMaterialModelEditorOnlyData::NotifyPostChange(const FPropertyChangedEvent& InPropertyChangedEvent, class FEditPropertyChain* InPropertyThatChanged)
{
	RequestMaterialBuild();
}

void UDynamicMaterialModelEditorOnlyData::PostLoad()
{
 	Super::PostLoad();

	// Backwards compatibility change - materials were originally parented to this object instead of the model.
	if (IsValid(MaterialModel))
	{
		if (UMaterial* Material = MaterialModel->GetGeneratedMaterial())
		{
			if (Material->GetOuter() != MaterialModel)
			{
				Material->Rename(nullptr, MaterialModel, UE::DynamicMaterial::RenameFlags);
			}
		}
	}

	SetFlags(RF_Transactional);

	AssignPropertyAlphaValues();

	ReinitComponents();
}

void UDynamicMaterialModelEditorOnlyData::PostEditUndo()
{
	Super::PostEditUndo();

	RequestMaterialBuild();
}

void UDynamicMaterialModelEditorOnlyData::PostEditImport()
{
	Super::PostEditImport();

	PostEditorDuplicate();
	ReinitComponents();
	RequestMaterialBuild();
}

void UDynamicMaterialModelEditorOnlyData::PostDuplicate(bool bInDuplicateForPIE)
{
	Super::PostDuplicate(bInDuplicateForPIE);

	if (!bInDuplicateForPIE)
	{
		PostEditorDuplicate();
		ReinitComponents();
		RequestMaterialBuild();
	}
}

void UDynamicMaterialModelEditorOnlyData::PostEditChangeChainProperty(FPropertyChangedChainEvent& InPropertyChangedEvent)
{
	UObject::PostEditChangeChainProperty(InPropertyChangedEvent);

	const FName Property = InPropertyChangedEvent.GetMemberPropertyName();

	 if (Property == GET_MEMBER_NAME_CHECKED(ThisClass, Domain))
	{
		OnDomainChanged();
	}
	else if (Property == GET_MEMBER_NAME_CHECKED(ThisClass, BlendMode))
	{
		OnBlendModeChanged();
	}
	else if (Property == GET_MEMBER_NAME_CHECKED(ThisClass, ShadingModel))
	{
		OnShadingModelChanged();
	}
	else if (Property == GET_MEMBER_NAME_CHECKED(ThisClass, bHasPixelAnimation)
		|| Property == GET_MEMBER_NAME_CHECKED(ThisClass, bTwoSided)
		|| Property == GET_MEMBER_NAME_CHECKED(ThisClass, bOutputTranslucentVelocityEnabled)
		|| Property == GET_MEMBER_NAME_CHECKED(ThisClass, bNaniteTessellationEnabled)
		|| Property == GET_MEMBER_NAME_CHECKED(ThisClass, bResponsiveAAEnabled))
	{
		OnMaterialFlagChanged();
	}
}

void UDynamicMaterialModelEditorOnlyData::OnValueUpdated(UDMMaterialValue* InValue, EDMUpdateType InUpdateType)
{
	check(InValue);

	// Non-exported materials have their values update via settings parameters
	// Exported materials need to be rebuilt to update the main material.
	const bool bMaterialInDifferentPackage = MaterialModel->DynamicMaterial ? MaterialModel->DynamicMaterial->GetPackage() != GetPackage() : true;

	if (EnumHasAnyFlags(InUpdateType, EDMUpdateType::Structure) || bMaterialInDifferentPackage)
	{
		RequestMaterialBuild();
	}
}

void UDynamicMaterialModelEditorOnlyData::OnTextureUVUpdated(UDMTextureUV* InTextureUV)
{
	check(InTextureUV);

	// Non-exported materials have their values update via settings parameters
	// Exported materials need to be rebuilt to update the main material.
	if (MaterialModel->DynamicMaterial && MaterialModel->DynamicMaterial->GetPackage() != GetPackage())
	{
		RequestMaterialBuild();
	}
}

void UDynamicMaterialModelEditorOnlyData::SaveEditor()
{
	UEditorLoadingAndSavingUtils::SavePackages({GetPackage()}, false);

	if (IsValid(MaterialModel) && IsValid(MaterialModel->DynamicMaterial))
	{
		if (MaterialModel->DynamicMaterial->GetPackage() != GetPackage())
		{
			UEditorLoadingAndSavingUtils::SavePackages({MaterialModel->DynamicMaterial->GetPackage()}, false);
		}
	}
}

FString UDynamicMaterialModelEditorOnlyData::GetMaterialAssetPath() const
{
	return FPaths::GetPath(GetPackage()->GetPathName());
}

FString UDynamicMaterialModelEditorOnlyData::GetMaterialAssetName() const
{
	return GetName() + "_Mat";
}

FString UDynamicMaterialModelEditorOnlyData::GetMaterialPackageName(const FString& InMaterialBaseName) const
{
	return GetPackage()->GetName() + "_Mat";
}

void UDynamicMaterialModelEditorOnlyData::OnSlotConnectorsUpdated(UDMMaterialSlot* InSlot)
{
	check(InSlot);

	RequestMaterialBuild();
		
	TArray<EDMMaterialPropertyType> SlotProperties = GetMaterialPropertiesForSlot(InSlot);

	for (EDMMaterialPropertyType SlotProperty : SlotProperties)
	{
		if (UDMMaterialProperty* Property = GetMaterialProperty(SlotProperty))
		{
			Property->ResetInputConnectionMap();
		}		
	}
}

void UDynamicMaterialModelEditorOnlyData::ReinitComponents()
{
	for (int32 SlotIdx = 0; SlotIdx < Slots.Num(); ++SlotIdx)
	{
		if (IsValid(Slots[SlotIdx]))
		{
			Slots[SlotIdx]->GetOnConnectorsUpdateDelegate().AddUObject(this, &UDynamicMaterialModelEditorOnlyData::OnSlotConnectorsUpdated);
		}
		else
		{
			Slots.RemoveAt(SlotIdx);
			--SlotIdx;
		}
	}
}

void UDynamicMaterialModelEditorOnlyData::PostEditorDuplicate()
{
	if (GUndo)
	{
		Modify();
	}

	UE::DynamicMaterial::ForEachMaterialPropertyType(
		[this](EDMMaterialPropertyType InType)
		{
			if (UDMMaterialProperty* Property = GetMaterialProperty(InType))
			{
				if (GUndo)
				{
					Property->Modify();
				}

				Property->PostEditorDuplicate(MaterialModel, nullptr);
			}

			return EDMIterationResult::Continue;
		}
	);

	for (UDMMaterialSlot* Slot : Slots)
	{
		if (GUndo)
		{
			Slot->Modify();
		}

		Slot->PostEditorDuplicate(MaterialModel, nullptr);
	}

	PropertySlotMap.Empty();

	for (UDMMaterialSlot* Slot : Slots)
	{
		const TArray<TObjectPtr<UDMMaterialLayerObject>>& SlotLayers = Slot->GetLayers();

		for (const TObjectPtr<UDMMaterialLayerObject>& Layer : SlotLayers)
		{
			EDMMaterialPropertyType Property = Layer->GetMaterialProperty();
			TObjectPtr<UDMMaterialSlot>* SlotPtr = PropertySlotMap.Find(Property);

			if (!SlotPtr)
			{
				PropertySlotMap.Emplace(Property, Slot);
			}
			else
			{
				check(*SlotPtr == Slot);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
