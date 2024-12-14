// Copyright Epic Games, Inc. All Rights Reserved.
#include "InterchangeProjectSettings.h"

#include "InterchangeManager.h"
#include "InterchangeSourceData.h"

#if WITH_EDITOR
void UInterchangeProjectSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	FProperty* PropertyThatChanged = PropertyChangedEvent.MemberProperty;
	const FString PropertyName = PropertyThatChanged ? PropertyThatChanged->GetName() : TEXT("");

	if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(UInterchangeProjectSettings, InterchangeGroups))
	{
		if (UInterchangeEditorSettings* InterchangeEditorSettings = GetMutableDefault<UInterchangeEditorSettings>())
		{
			InterchangeEditorSettings->UpdateUsedGroupName();
		}
	}
}
#endif

const FInterchangeImportSettings& FInterchangeProjectSettingsUtils::GetImportSettings(const UInterchangeProjectSettings& InterchangeProjectSettings, const bool bIsSceneImport)
{
	if (bIsSceneImport)
	{
		return InterchangeProjectSettings.SceneImportSettings;
	}
	else
	{
		return InterchangeProjectSettings.ContentImportSettings;
	}
}

FInterchangeImportSettings& FInterchangeProjectSettingsUtils::GetMutableImportSettings(UInterchangeProjectSettings& InterchangeProjectSettings, const bool bIsSceneImport)
{
	if (bIsSceneImport)
	{
		return InterchangeProjectSettings.SceneImportSettings;
	}
	else
	{
		return InterchangeProjectSettings.ContentImportSettings;
	}
}

const FInterchangeImportSettings& FInterchangeProjectSettingsUtils::GetDefaultImportSettings(const bool bIsSceneImport)
{
	return GetImportSettings(*GetDefault<UInterchangeProjectSettings>(), bIsSceneImport);
}

FInterchangeImportSettings& FInterchangeProjectSettingsUtils::GetMutableDefaultImportSettings(const bool bIsSceneImport)
{
	return GetMutableImportSettings(*GetMutableDefault<UInterchangeProjectSettings>(), bIsSceneImport);
}

FName FInterchangeProjectSettingsUtils::GetDefaultPipelineStackName(const bool bIsSceneImport, const UInterchangeSourceData& SourceData)
{
	const FInterchangeImportSettings& ImportSettings = GetDefaultImportSettings(bIsSceneImport);
	
	FInterchangeGroup::EUsedGroupStatus UsedGroupStatus;
	const FInterchangeGroup& UsedInterchangeGroup = FInterchangeProjectSettingsUtils::GetUsedGroup(UsedGroupStatus);
	bool bInterchangeGroupUsed = (UsedGroupStatus == FInterchangeGroup::EUsedGroupStatus::SetAndValid);

	FName DefaultPipelineStack = bInterchangeGroupUsed ? UsedInterchangeGroup.DefaultPipelineStack : ImportSettings.DefaultPipelineStack;

	if (!bIsSceneImport)
	{
		UE::Interchange::FScopedTranslator ScopedTranslator(&SourceData);

		if (UInterchangeTranslatorBase* Translator = ScopedTranslator.GetTranslator())
		{
			EInterchangeTranslatorAssetType SupportedAssetTypes = Translator->GetSupportedAssetTypes();

			const TMap<EInterchangeTranslatorAssetType, FName>& DefaultPipelineStackOverride = bInterchangeGroupUsed ? UsedInterchangeGroup.DefaultPipelineStackOverride : GetDefault<UInterchangeProjectSettings>()->ContentImportSettings.DefaultPipelineStackOverride;

			for (TMap<EInterchangeTranslatorAssetType, FName>::TConstIterator StackOverridesIt = DefaultPipelineStackOverride.CreateConstIterator(); StackOverridesIt; ++StackOverridesIt)
			{
				if ((SupportedAssetTypes ^ StackOverridesIt->Key) < StackOverridesIt->Key)
				{
					DefaultPipelineStack = StackOverridesIt->Value;
					break;
				}
			}
		}
	}

	return DefaultPipelineStack;
}

void FInterchangeProjectSettingsUtils::SetDefaultPipelineStackName(const bool bIsSceneImport, const UInterchangeSourceData& SourceData, const FName StackName)
{
	FInterchangeImportSettings& ImportSettings = GetMutableDefaultImportSettings(bIsSceneImport);

	if (!ImportSettings.PipelineStacks.Contains(StackName))
	{
		//The new stack name must be valid
		return;
	}

	FName DefaultPipelineStack = ImportSettings.DefaultPipelineStack;

	if (!bIsSceneImport)
	{
		UE::Interchange::FScopedTranslator ScopedTranslator(&SourceData);

		if (UInterchangeTranslatorBase* Translator = ScopedTranslator.GetTranslator())
		{
			EInterchangeTranslatorAssetType SupportedAssetTypes = Translator->GetSupportedAssetTypes();

			FInterchangeContentImportSettings& ContentImportSettings = GetMutableDefault<UInterchangeProjectSettings>()->ContentImportSettings;
			for (TMap<EInterchangeTranslatorAssetType, FName>::TIterator StackOverridesIt = ContentImportSettings.DefaultPipelineStackOverride.CreateIterator(); StackOverridesIt; ++StackOverridesIt)
			{
				if ((SupportedAssetTypes ^ StackOverridesIt->Key) < StackOverridesIt->Key)
				{
					//Update the override stack name and save the config
					StackOverridesIt->Value = StackName;
					GetMutableDefault<UInterchangeProjectSettings>()->SaveConfig();
					return;
				}
			}
		}
	}

	//We do not have any override default stack, simply change the DefaultPipelineStack and save the config
	ImportSettings.DefaultPipelineStack = StackName;
	GetMutableDefault<UInterchangeProjectSettings>()->SaveConfig();
}

bool FInterchangeProjectSettingsUtils::ShouldShowPipelineStacksConfigurationDialog(const bool bIsSceneImport, const bool bReImport, const UInterchangeSourceData& SourceData)
{
	bool bShowImportDialog = true; 

	FInterchangeGroup::EUsedGroupStatus UsedGroupStatus;
	const FInterchangeGroup& UsedInterchangeGroup = FInterchangeProjectSettingsUtils::GetUsedGroup(UsedGroupStatus);
	bool bInterchangeGroupUsed = (UsedGroupStatus == FInterchangeGroup::EUsedGroupStatus::SetAndValid);

	bShowImportDialog = bInterchangeGroupUsed ? UsedInterchangeGroup.bShowImportDialog : GetDefaultImportSettings(bIsSceneImport).bShowImportDialog;
	

	if (!bIsSceneImport)
	{
		UE::Interchange::FScopedTranslator ScopedTranslator(&SourceData);

		if (UInterchangeTranslatorBase* Translator = ScopedTranslator.GetTranslator())
		{
			EInterchangeTranslatorAssetType SupportedAssetTypes = Translator->GetSupportedAssetTypes();
			const UClass* TranslatorClass = Translator->GetClass();

			//Iterate all override, if there is at least one override that show the imnport dialog we will show it.
			bool bFoundOverride = false;
			bool bShowFromOverrideStack = false;
			const TMap<EInterchangeTranslatorAssetType, FInterchangeDialogOverride>& ShowImportDialogOverride = bInterchangeGroupUsed ? UsedInterchangeGroup.ShowImportDialogOverride : GetDefault<UInterchangeProjectSettings>()->ContentImportSettings.ShowImportDialogOverride;

			for (const TPair<EInterchangeTranslatorAssetType, FInterchangeDialogOverride>& ShowImportDialog : ShowImportDialogOverride)
			{
				if ((ShowImportDialog.Key == EInterchangeTranslatorAssetType::None && SupportedAssetTypes == EInterchangeTranslatorAssetType::None)
					|| (static_cast<uint8>(ShowImportDialog.Key & SupportedAssetTypes) > 0))
				{
					//Look if there is a per translator override
					const FInterchangeDialogOverride& InterchangeDialogOverride = ShowImportDialog.Value;
					const FInterchangePerTranslatorDialogOverride* FindPerTranslatorOverride = InterchangeDialogOverride.PerTranslatorImportDialogOverride.FindByPredicate([&TranslatorClass](const FInterchangePerTranslatorDialogOverride& InterchangePerTranslatorDialogOverride)
						{
							return (InterchangePerTranslatorDialogOverride.Translator.Get() == TranslatorClass);
						});
					if (FindPerTranslatorOverride)
					{
						bShowFromOverrideStack |= FindPerTranslatorOverride->bShowImportDialog;
					}
					else
					{
						//Simple override
						bShowFromOverrideStack |= InterchangeDialogOverride.bShowImportDialog;
					}
					bFoundOverride = true;
				}
			}
			if (bFoundOverride)
			{
				bShowImportDialog = bShowFromOverrideStack;
			}
		}
	}

	if (const UInterchangeEditorSettings* InterchangeEditorSettings = GetDefault<UInterchangeEditorSettings>())
	{
		bShowImportDialog &= (bReImport ? InterchangeEditorSettings->bShowImportDialogAtReimport : true);
	}

	return bShowImportDialog;
}

const FInterchangeGroup& FInterchangeProjectSettingsUtils::GetUsedGroup(FInterchangeGroup::EUsedGroupStatus& UsedGroupStatus)
{
	static const FInterchangeGroup InterchangeGroupNone;

	UsedGroupStatus = FInterchangeGroup::EUsedGroupStatus::NotSet;

	if (const UInterchangeEditorSettings* InterchangeEditorSettings = GetDefault<UInterchangeEditorSettings>())
	{
		const FGuid& InterchangeUsedGroupUID = InterchangeEditorSettings->GetUsedGroupUID();
		if (InterchangeUsedGroupUID.IsValid())
		{
			const UInterchangeProjectSettings* ImportSettings = GetDefault<UInterchangeProjectSettings>();
			for (const FInterchangeGroup& Group : ImportSettings->InterchangeGroups)
			{
				if (Group.UniqueID == InterchangeUsedGroupUID)
				{
					UsedGroupStatus = FInterchangeGroup::EUsedGroupStatus::SetAndValid;
					return Group;
				}
			}

			UsedGroupStatus = FInterchangeGroup::EUsedGroupStatus::SetAndInvalid;
		}
	}

	return InterchangeGroupNone;
}

TArray<FName> FInterchangeProjectSettingsUtils::GetGroupNames()
{
	TArray<FName> GroupNames;
	GroupNames.Add(FName());

	const TArray<FInterchangeGroup>& Groups = GetDefault<UInterchangeProjectSettings>()->InterchangeGroups;
	for (const FInterchangeGroup& Group : Groups)
	{
		GroupNames.Add(Group.DisplayName);
	}

	return GroupNames;
}

void UInterchangeEditorSettings::SetUsedGroupName(const FName& InUsedGroupName)
{
	UsedGroupName = InUsedGroupName;

	UpdateUsedGroupUIDFromGroupName();
}

TArray<FName> UInterchangeEditorSettings::GetSelectableItems() const
{
	return FInterchangeProjectSettingsUtils::GetGroupNames();
}

void UInterchangeEditorSettings::UpdateUsedGroupName()
{
	FInterchangeGroup::EUsedGroupStatus UsedGroupStatus;
	const FInterchangeGroup& UsedInterchangeGroup = FInterchangeProjectSettingsUtils::GetUsedGroup(UsedGroupStatus);

	switch (UsedGroupStatus)
	{
		case FInterchangeGroup::NotSet:
			UsedGroupName = FName();
			break;
		case FInterchangeGroup::SetAndValid:
			UsedGroupName = UsedInterchangeGroup.DisplayName;
			break;
		case FInterchangeGroup::SetAndInvalid:
			UsedGroupName = FName("Invalid Group Used, Defaulting to No Group usage.");
			break;
		default:
			break;
	}
}

void UInterchangeEditorSettings::UpdateUsedGroupUIDFromGroupName()
{
	UsedGroupUID = FGuid();

	const UInterchangeProjectSettings* ImportSettings = GetDefault<UInterchangeProjectSettings>();
	for (const FInterchangeGroup& Group : ImportSettings->InterchangeGroups)
	{
		if (Group.DisplayName == UsedGroupName)
		{
			UsedGroupUID = Group.UniqueID;
		}
	}

	UpdateUsedGroupName();
}

#if WITH_EDITOR
void UInterchangeEditorSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	FProperty* PropertyThatChanged = PropertyChangedEvent.MemberProperty;
	const FString PropertyName = PropertyThatChanged ? PropertyThatChanged->GetName() : TEXT("");

	if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(UInterchangeEditorSettings, UsedGroupName))
	{
		UpdateUsedGroupUIDFromGroupName();
	}
}

void UInterchangeEditorSettings::PostInitProperties()
{
	Super::PostInitProperties();

	UpdateUsedGroupName();
}

#endif