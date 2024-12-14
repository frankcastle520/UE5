// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorValidator_Material.h"
#include "AssetCompilingManager.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "DetailWidgetRow.h"
#include "EditorValidatorSubsystem.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Misc/DataValidation.h"
#include "Widgets/Input/SComboBox.h"

UEditorValidator_Material::UEditorValidator_Material()
	: Super()
{
	if (GetDefault<UDataValidationSettings>()->bEnableMaterialValidation)
	{
		for (const FMaterialEditorValidationPlatform& Config: GetDefault<UDataValidationSettings>()->MaterialValidationPlatforms)
		{
			FShaderValidationPlatform Platform = {};

			bool bValidShaderPlatform = false;
			if (Config.ShaderPlatform.Name == FMaterialEditorValidationShaderPlatform::MaxRHIShaderPlatformName)
			{
				Platform.ShaderPlatform = GMaxRHIShaderPlatform;
				bValidShaderPlatform = true;
			}
			else
			{
				for (int32 ShaderPlatformIndex = 0; ShaderPlatformIndex < SP_NumPlatforms; ++ShaderPlatformIndex)
				{
					const EShaderPlatform ShaderPlatform = static_cast<EShaderPlatform>(ShaderPlatformIndex);

					if (FDataDrivenShaderPlatformInfo::IsValid(ShaderPlatform)
						&& FDataDrivenShaderPlatformInfo::CanUseForMaterialValidation(ShaderPlatform)
						&& FDataDrivenShaderPlatformInfo::GetName(ShaderPlatform) == Config.ShaderPlatform.Name)
					{
						Platform.ShaderPlatform = ShaderPlatform;
						bValidShaderPlatform = true;
						break;
					}
				}
			}

			if (!bValidShaderPlatform)
			{
				UE_LOG(LogContentValidation, Warning, TEXT("Material asset validation shader platform '%s' is not available, skipping."), *Config.ShaderPlatform.Name.ToString());
				continue;
			}

			switch (Config.FeatureLevel)
			{
			case EMaterialEditorValidationFeatureLevel::CurrentMaxFeatureLevel: Platform.FeatureLevel = GMaxRHIFeatureLevel; break; 
			case EMaterialEditorValidationFeatureLevel::ES3_1: Platform.FeatureLevel = ERHIFeatureLevel::ES3_1; break;
			case EMaterialEditorValidationFeatureLevel::SM5: Platform.FeatureLevel = ERHIFeatureLevel::SM5; break;
			case EMaterialEditorValidationFeatureLevel::SM6: Platform.FeatureLevel = ERHIFeatureLevel::SM6; break;
			}

			switch (Config.MaterialQualityLevel)
			{
			case EMaterialEditorValidationQualityLevel::Low: Platform.MaterialQualityLevel = EMaterialQualityLevel::Low; break;
			case EMaterialEditorValidationQualityLevel::Medium: Platform.MaterialQualityLevel = EMaterialQualityLevel::Medium; break;
			case EMaterialEditorValidationQualityLevel::High: Platform.MaterialQualityLevel = EMaterialQualityLevel::High; break;
			case EMaterialEditorValidationQualityLevel::Epic: Platform.MaterialQualityLevel = EMaterialQualityLevel::Epic; break;
			}

			ValidationPlatforms.Add(Platform);
		}
	}
}

bool UEditorValidator_Material::CanValidateAsset_Implementation(const FAssetData& AssetData, UObject* InAsset, FDataValidationContext& InContext) const
{
	if (InContext.GetValidationUsecase() == EDataValidationUsecase::Save || InContext.GetValidationUsecase() == EDataValidationUsecase::Commandlet)
	{
		return false;
	}

	if (ValidationPlatforms.IsEmpty())
	{
		return false;
	}

	const UMaterial* OriginalMaterial = Cast<UMaterial>(InAsset);
	if (OriginalMaterial != nullptr) // we can always validate a UMaterial
	{
		return true;
	}

	const UMaterialInstance* OriginalMaterialInstance = Cast<UMaterialInstance>(InAsset);
	if (OriginalMaterialInstance != nullptr)
	{
		FMaterialInheritanceChain Chain;
		OriginalMaterialInstance->GetMaterialInheritanceChain(Chain);

		for (const UMaterialInstance* MaterialInstance: Chain.MaterialInstances)
		{
			if (MaterialInstance->HasStaticParameters())
			{
				// only validate UMaterialInstance if it influences shader compilation
				return true;
			}
		}
	}

	return false;
}

EDataValidationResult UEditorValidator_Material::ValidateLoadedAsset_Implementation(const FAssetData& AssetData, UObject* InAsset, FDataValidationContext& InContext)
{
	UMaterialInstance* OriginalMaterialInstance = Cast<UMaterialInstance>(InAsset);
	UMaterial* OriginalMaterial = OriginalMaterialInstance ? OriginalMaterialInstance->GetMaterial() : Cast<UMaterial>(InAsset);

	UMaterialInstance* MaterialInstance = DuplicateMaterialInstance(OriginalMaterialInstance);
	UMaterial* Material = MaterialInstance ? MaterialInstance->GetMaterial() : DuplicateMaterial(OriginalMaterial);
	if (!ensureAlways(OriginalMaterial) || !ensureAlways(Material))
	{
		return EDataValidationResult::NotValidated;
	}

	TArray<FMaterialResource*> Resources;

	for (const FShaderValidationPlatform& ValidationPlatform: ValidationPlatforms)
	{
		FMaterialResource* CurrentResource = FindOrCreateMaterialResource(Resources, Material, MaterialInstance, ValidationPlatform.FeatureLevel, ValidationPlatform.MaterialQualityLevel);

		if (ensure(CurrentResource))
		{
			CurrentResource->CacheShaders(ValidationPlatform.ShaderPlatform);
		}
	}

	if (Resources.IsEmpty())
	{
		return EDataValidationResult::NotValidated;
	}

	FAssetCompilingManager::Get().FinishAllCompilation();

	bool bCompileErrors = false;
	for (FMaterialResource* Resource: Resources)
	{
		if (!Resource->IsCompilationFinished())
		{
			UE_LOG(LogContentValidation, Warning, TEXT("Shader compilation was expected to be finished, but was not finished."));
		}

		bCompileErrors |= Resource->GetCompileErrors().Num() > 0;
	}

	FMaterial::DeferredDeleteArray(Resources);

	return !bCompileErrors ? EDataValidationResult::Valid : EDataValidationResult::Invalid;
}

UMaterial* UEditorValidator_Material::DuplicateMaterial(UMaterial* OriginalMaterial)
{
	if (!OriginalMaterial)
	{
		return nullptr;
	}

	return static_cast<UMaterial*>(StaticDuplicateObject(OriginalMaterial, GetTransientPackage(), NAME_None, ~RF_Standalone, UValidationMaterial::StaticClass()));
}

UMaterialInstance* UEditorValidator_Material::DuplicateMaterialInstance(UMaterialInstance* OriginalMaterialInstance)
{
	if (!OriginalMaterialInstance)
	{
		return nullptr;
	}

	TArray<UMaterialInstance*> DuplicatedMaterialInstances;

	FMaterialInheritanceChain Chain;
	OriginalMaterialInstance->GetMaterialInheritanceChain(Chain);

	for (const UMaterialInstance* MaterialInstance: Chain.MaterialInstances)
	{
		// only duplicate material instances that might influence compilation
		if (!MaterialInstance->HasStaticParameters())
		{
			continue;
		}

		UMaterialInstance* DuplicatedMaterialInstance = Cast<UMaterialInstance>(StaticDuplicateObject(MaterialInstance, GetTransientPackage(), NAME_None, ~RF_Standalone, MaterialInstance->GetClass()));

		DuplicatedMaterialInstances.Add(DuplicatedMaterialInstance);
	}

	// should be caught by CanValidateAsset_Implementation
	if (ensureAlways(DuplicatedMaterialInstances.Num() > 0))
	{
		UMaterial* DuplicatedMaterial = DuplicateMaterial(OriginalMaterialInstance->GetMaterial());

		for (int32 i = 0; i < DuplicatedMaterialInstances.Num(); ++i)
		{
			if (i + 1 < DuplicatedMaterialInstances.Num())
			{
				DuplicatedMaterialInstances[i]->Parent = DuplicatedMaterialInstances[i + 1];
			}
			else
			{
				DuplicatedMaterialInstances[i]->Parent = DuplicatedMaterial;
			}
		}

		return DuplicatedMaterialInstances[0];
	}
	else
	{
		return nullptr;
	}
}

class FValidationMaterial : public FMaterialResource
{
public:
	FValidationMaterial() = default;
	virtual ~FValidationMaterial() override = default;

	virtual bool IsPersistent() const override { return false; }
	virtual FString GetAssetName() const override { return FString::Printf(TEXT("Validation:%s"), *FMaterialResource::GetAssetName()); }
	virtual bool IsPreview() const override { return true; }
};

FMaterialResource* UValidationMaterial::AllocateResource()
{
	return new FValidationMaterial();
}

class FMaterialEditorValidationPlatformCustomization : public IPropertyTypeCustomization
{
public:
	FMaterialEditorValidationPlatformCustomization()
		: MaxRHIShaderPlatform(MakeShared<EShaderPlatform>(SP_NumPlatforms))
	{
		for (int32 ShaderPlatformIndex = 0; ShaderPlatformIndex < SP_NumPlatforms; ++ShaderPlatformIndex)
		{
			const EShaderPlatform ShaderPlatform = static_cast<EShaderPlatform>(ShaderPlatformIndex);

			if (FDataDrivenShaderPlatformInfo::IsValid(ShaderPlatform) && FDataDrivenShaderPlatformInfo::CanUseForMaterialValidation(ShaderPlatform))
			{
				ValidationShaderPlatforms.Add(MakeShared<EShaderPlatform>(ShaderPlatform));
			}
		}

		ValidationShaderPlatforms.StableSort([this](const TSharedPtr<EShaderPlatform>& A, const TSharedPtr<EShaderPlatform>& B)
		{
			return GetShaderPlatformFriendlyName(A).CompareTo(GetShaderPlatformFriendlyName(B)) < 0;
		});

		ValidationShaderPlatforms.Insert(MaxRHIShaderPlatform, 0);
	}

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override
	{
		TSharedPtr<IPropertyHandle> PropertyHandle = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMaterialEditorValidationShaderPlatform, Name));
		ensure(PropertyHandle.IsValid());
		if (!PropertyHandle.IsValid())
		{
			return;
		}

		HeaderRow
		.NameContent()
		[
			StructPropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		[
			SNew(SComboBox<TSharedPtr<EShaderPlatform>>)
			.OptionsSource(&ValidationShaderPlatforms)
			.InitiallySelectedItem(GetCurrentShaderPlatform(PropertyHandle))
			.OnSelectionChanged_Lambda([this, PropertyHandle](TSharedPtr<EShaderPlatform> ShaderPlatformOpt, ESelectInfo::Type SelectionInfo)
			{
				if (ShaderPlatformOpt.IsValid() && PropertyHandle.IsValid())
				{
					PropertyHandle->NotifyPreChange();
					if (ShaderPlatformOpt == MaxRHIShaderPlatform)
					{
						PropertyHandle->SetValue(FMaterialEditorValidationShaderPlatform::MaxRHIShaderPlatformName);
					}
					else
					{
						PropertyHandle->SetValue(FDataDrivenShaderPlatformInfo::GetName(*ShaderPlatformOpt.Get()));
					}
					PropertyHandle->NotifyPostChange(EPropertyChangeType::ValueSet);
				}
			})
			.OnGenerateWidget_Lambda([this](const TSharedPtr<EShaderPlatform>& ShaderPlatformOpt)
			{
				return SNew(STextBlock).Text(GetShaderPlatformFriendlyName(ShaderPlatformOpt));
			})
			.Content()
			[
				SNew(STextBlock)
				.Font(StructCustomizationUtils.GetRegularFont())
				.Text_Lambda([this, PropertyHandle]()
				{
					return GetShaderPlatformFriendlyName(GetCurrentShaderPlatform(PropertyHandle)); 
				})
			]
		];
	}

	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override
	{
	}

private:
	TArray<TSharedPtr<EShaderPlatform>> ValidationShaderPlatforms;
	const TSharedPtr<EShaderPlatform> MaxRHIShaderPlatform;

	FName GetShaderPlatformName(const TSharedPtr<EShaderPlatform>& ShaderPlatformOpt)
	{
		if (ShaderPlatformOpt == MaxRHIShaderPlatform)
		{
			return FMaterialEditorValidationShaderPlatform::MaxRHIShaderPlatformName;
		}
		else if (ShaderPlatformOpt.IsValid())
		{
			return FDataDrivenShaderPlatformInfo::GetName(*ShaderPlatformOpt.Get());
		}
		else
		{
			return FName("Invalid");
		}
	}

	FText GetShaderPlatformFriendlyName(const TSharedPtr<EShaderPlatform>& ShaderPlatformOpt)
	{
		if (ShaderPlatformOpt == MaxRHIShaderPlatform)
		{
			return NSLOCTEXT("AssetValidation", "ShaderPlatform_MaxRHIShaderPlatform", "Current RHI Max Shader Platform");
		}
		else if (ShaderPlatformOpt.IsValid())
		{
			const FText Name = FDataDrivenShaderPlatformInfo::GetFriendlyName(*ShaderPlatformOpt.Get());
			if (!Name.IsEmpty())
			{
				return Name;
			}

			return FText::FromName(FDataDrivenShaderPlatformInfo::GetName(*ShaderPlatformOpt.Get()));
		}
		else
		{
			return NSLOCTEXT("AssetValidation", "ShaderPlatform_Invalid", "Invalid");
		}
	}

	TSharedPtr<EShaderPlatform> GetCurrentShaderPlatform(const TSharedPtr<IPropertyHandle>& PropertyHandle)
	{
		FName CurrentShaderPlatformName;
		if (PropertyHandle->GetValue(CurrentShaderPlatformName))
		{
			for (const TSharedPtr<EShaderPlatform>& ValidationShaderPlatform : ValidationShaderPlatforms)
			{
				if (GetShaderPlatformName(ValidationShaderPlatform) == CurrentShaderPlatformName)
				{
					return ValidationShaderPlatform;
				}
			}
		}

		return TSharedPtr<EShaderPlatform>();
	}
};

FName FMaterialEditorValidationShaderPlatform::MaxRHIShaderPlatformName = MaxRHIShaderPlatformNameView.GetData();
FName FMaterialEditorValidationShaderPlatform::CustomPropertyTypeLayoutName;

void FMaterialEditorValidationShaderPlatform::RegisterCustomPropertyTypeLayout()
{
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	CustomPropertyTypeLayoutName = StaticStruct()->GetFName(); 
	PropertyModule.RegisterCustomPropertyTypeLayout(
		StaticStruct()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateLambda([](){return MakeShared<FMaterialEditorValidationPlatformCustomization>();})
	);
}

void FMaterialEditorValidationShaderPlatform::UnregisterCustomPropertyTypeLayout()
{
	FPropertyEditorModule* PropertyModule = FModuleManager::GetModulePtr<FPropertyEditorModule>("PropertyEditor");
	if (PropertyModule)
	{
		// StaticStruct()->GetFName() is not available during engine shutdown as UObjects were already destroyed
		PropertyModule->UnregisterCustomPropertyTypeLayout(CustomPropertyTypeLayoutName);
	}
}
