// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorValidator_PluginAssetReferences.h"

#include "Algo/RemoveIf.h"
#include "AssetReferencingPolicySubsystem.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetDataToken.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "DataValidationChangelist.h"
#include "Editor.h"
#include "EditorValidatorSubsystem.h"
#include "Editor/EditorEngine.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/PathViews.h"

bool UEditorValidator_PluginAssetReferences::CanValidateAsset_Implementation(const FAssetData& InAssetData, UObject* InObject, FDataValidationContext& InContext) const
{
    if (UDataValidationChangelist* Changelist = Cast<UDataValidationChangelist>(InObject))
    {
        for (const FString& ModifiedFilePath : Changelist->ModifiedFiles)
        {
            if (FPathViews::GetExtension(ModifiedFilePath) == TEXTVIEW("uplugin"))
            {
                return true;
            }
        }
    }
    return false;
}

EDataValidationResult UEditorValidator_PluginAssetReferences::ValidateLoadedAsset_Implementation(const FAssetData& InAssetData, UObject* InAsset, FDataValidationContext& Context) 
{
    UDataValidationChangelist* Changelist = Cast<UDataValidationChangelist>(InAsset);
    if (!Changelist)
    {
        return EDataValidationResult::Valid;
    }

    IPluginManager& PluginManager = IPluginManager::Get();
    TArray<TSharedRef<IPlugin>> PluginsToValidate;
    for (const FString& ModifiedFilePath : Changelist->ModifiedFiles)
    {
        if (FPathViews::GetExtension(ModifiedFilePath) == TEXTVIEW("uplugin"))
        {
            FStringView PluginName = FPathViews::GetBaseFilename(ModifiedFilePath);
            TSharedPtr<IPlugin> Plugin = PluginManager.FindPlugin(PluginName);
            if (Plugin.IsValid() && Plugin->IsEnabled())
            {
                PluginsToValidate.Add(Plugin.ToSharedRef());
            }
        }
    }

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
    TArray<FAssetData> AssetsToValidate;
    for (const TSharedRef<IPlugin>& Plugin : PluginsToValidate)
    {
        TStringBuilder<256> RootPath(InPlace, TEXT("/"), Plugin->GetName());
        AssetRegistry.GetAssetsByPath(FName(RootPath), AssetsToValidate, true, true);
    }

    UAssetReferencingPolicySubsystem* AssetReferencingPolicySubsystem = GEditor->GetEditorSubsystem<UAssetReferencingPolicySubsystem>();
	UEditorValidatorSubsystem* EditorValidationSubsystem = GEditor->GetEditorSubsystem<UEditorValidatorSubsystem>();
	FValidateAssetsSettings Settings;
	FDataValidationContext ValidationContext(false, Settings.ValidationUsecase, {});
    AssetsToValidate.SetNum(Algo::RemoveIf(AssetsToValidate, [AssetReferencingPolicySubsystem, EditorValidationSubsystem, &Settings, &ValidationContext](const FAssetData& Asset) {
        return !AssetReferencingPolicySubsystem->ShouldValidateAssetReferences(Asset) || !EditorValidationSubsystem->ShouldValidateAsset(Asset, Settings, ValidationContext);
    }));

    for (const FAssetData& Asset : AssetsToValidate)
    {
        TValueOrError<void, TArray<FAssetReferenceError>> Result = AssetReferencingPolicySubsystem->ValidateAssetReferences(Asset);
        if (Result.HasError())
        {
            for (const FAssetReferenceError& Error : Result.GetError())
            {
                AssetMessage(Asset, EMessageSeverity::Error, Error.Message)->AddToken(FAssetDataToken::Create(Error.ReferencedAsset));
            }
        }
    }

    if(GetValidationResult() != EDataValidationResult::Invalid)
    {
        AssetPasses(InAsset);
    }
    return GetValidationResult();
}