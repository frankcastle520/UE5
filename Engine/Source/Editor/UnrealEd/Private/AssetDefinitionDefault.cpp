// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetDefinitionDefault.h"

#include "AssetDefinitionAssetInfo.h"
#include "AssetToolsModule.h"
#include "IAssetStatusInfoProvider.h"
#include "EditorFramework/AssetImportData.h"
#include "EditorFramework/ThumbnailInfo.h"
#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "Settings/EditorLoadingSavingSettings.h"
#include "Toolkits/SimpleAssetEditor.h"

#define LOCTEXT_NAMESPACE "AssetDefinitionDefault"

#if UE_CONTENTBROWSER_NEW_STYLE
namespace UE::AssetDefinitionDefault::Status
{
	EVisibility GetDirtyStatusVisibility(const TSharedPtr<IAssetStatusInfoProvider> InAssetStatusInfoProvider)
	{
		EVisibility DirtyStatusVisibility = EVisibility::Collapsed;
		if (!InAssetStatusInfoProvider.IsValid())
		{
			return DirtyStatusVisibility;
		}

		if (const UPackage* Package = InAssetStatusInfoProvider->FindPackage())
		{
			DirtyStatusVisibility = Package->IsDirty() ? EVisibility::Visible : EVisibility::Collapsed;
		}
		return DirtyStatusVisibility;
	}

	const FSlateBrush* GetSourceControlStatusBrush(const TSharedPtr<IAssetStatusInfoProvider> InAssetStatusInfoProvider)
	{
		const FSlateBrush* SourceControlBrush = FAppStyle::GetNoBrush();
		if (!InAssetStatusInfoProvider.IsValid())
		{
			return SourceControlBrush;
		}

		if (ISourceControlModule::Get().IsEnabled() && ISourceControlModule::Get().GetProvider().IsAvailable())
		{
			const FString FileName = InAssetStatusInfoProvider->TryGetFilename();
			if (FSourceControlStatePtr SourceControlState = ISourceControlModule::Get().GetProvider().GetState(FileName, EStateCacheUsage::Use))
			{
				FSlateIcon SCCIcon = SourceControlState->GetIcon();
				if (SCCIcon.IsSet())
				{
					SourceControlBrush = SCCIcon.GetIcon();
				}
			}
		}
		return SourceControlBrush;
	}

	EVisibility GetSourceControlStatusVisibility(const TSharedPtr<IAssetStatusInfoProvider> InAssetStatusInfoProvider)
	{
		EVisibility SourceControlStatusVisibility = EVisibility::Collapsed;
		if (!InAssetStatusInfoProvider.IsValid())
		{
			return SourceControlStatusVisibility;
		}

		if (ISourceControlModule::Get().IsEnabled() && ISourceControlModule::Get().GetProvider().IsAvailable())
		{
			const FString FileName = InAssetStatusInfoProvider->TryGetFilename();
			if (FSourceControlStatePtr SourceControlState = ISourceControlModule::Get().GetProvider().GetState(FileName, EStateCacheUsage::Use))
			{
				FSlateIcon SCCIcon = SourceControlState->GetIcon();
				if (SCCIcon.IsSet())
				{
					SourceControlStatusVisibility = EVisibility::Visible;
				}
			}
		}
		return SourceControlStatusVisibility;
	}

	FText GetSourceControlStatusDescription(const TSharedPtr<IAssetStatusInfoProvider> InAssetStatusInfoProvider)
	{
		FText SourceControlDescription = FText::GetEmpty();
		if (!InAssetStatusInfoProvider.IsValid())
		{
			return SourceControlDescription;
		}

		if (ISourceControlModule::Get().IsEnabled() && ISourceControlModule::Get().GetProvider().IsAvailable())
		{
			const FString FileName = InAssetStatusInfoProvider->TryGetFilename();
			if (FSourceControlStatePtr SourceControlState = ISourceControlModule::Get().GetProvider().GetState(FileName, EStateCacheUsage::Use))
			{
				TOptional<FText> StatusText = SourceControlState->GetStatusText();
				if (StatusText.IsSet())
				{
					SourceControlDescription = StatusText.GetValue();
				}
			}
		}
		return SourceControlDescription;
	}
}
#endif

EAssetCommandResult UAssetDefinitionDefault::OpenAssets(const FAssetOpenArgs& OpenArgs) const
{
	if (GetAssetOpenSupport(FAssetOpenSupportArgs(OpenArgs.OpenMethod)).IsSupported)
	{
		FSimpleAssetEditor::CreateEditor(EToolkitMode::Standalone, OpenArgs.ToolkitHost, OpenArgs.LoadObjects<UObject>());
		return EAssetCommandResult::Handled;
	}

	return EAssetCommandResult::Unhandled;
}

EAssetCommandResult UAssetDefinitionDefault::PerformAssetDiff(const FAssetDiffArgs& DiffArgs) const
{
	if (DiffArgs.OldAsset == nullptr && DiffArgs.NewAsset == nullptr)
	{
		return EAssetCommandResult::Unhandled;
	}
	
	IAssetTools& AssetTools = FModuleManager::Get().LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	// Dump assets to temp text files
	FString OldTextFilename = AssetTools.DumpAssetToTempFile(DiffArgs.OldAsset);
	FString NewTextFilename = AssetTools.DumpAssetToTempFile(DiffArgs.NewAsset);
	FString DiffCommand = GetDefault<UEditorLoadingSavingSettings>()->TextDiffToolPath.FilePath;

	AssetTools.CreateDiffProcess(DiffCommand, OldTextFilename, NewTextFilename);

	return EAssetCommandResult::Handled;
}

#if UE_CONTENTBROWSER_NEW_STYLE
void UAssetDefinitionDefault::GetAssetStatusInfo(const TSharedPtr<IAssetStatusInfoProvider>& InAssetStatusInfoProvider, TArray<FAssetDisplayInfo>& OutStatusInfo) const
{
	FAssetDisplayInfo DirtyStatus;
	DirtyStatus.StatusIcon = FAppStyle::GetBrush("ContentBrowser.ContentDirty");
	DirtyStatus.Priority = FAssetStatusPriority(EStatusSeverity::Info, 1);
	DirtyStatus.StatusDescription = LOCTEXT("DirtyAssetTooltip", "Asset has unsaved changes");
	DirtyStatus.IsVisible = TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateStatic(&UE::AssetDefinitionDefault::Status::GetDirtyStatusVisibility, InAssetStatusInfoProvider));
	OutStatusInfo.Add(DirtyStatus);

	FAssetDisplayInfo SCCStatus;
	SCCStatus.Priority = FAssetStatusPriority(EStatusSeverity::Info, 0);
	SCCStatus.StatusIcon = TAttribute<const FSlateBrush*>::Create(TAttribute<const FSlateBrush*>::FGetter::CreateStatic(&UE::AssetDefinitionDefault::Status::GetSourceControlStatusBrush, InAssetStatusInfoProvider));
	SCCStatus.IsVisible = TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateStatic(&UE::AssetDefinitionDefault::Status::GetSourceControlStatusVisibility, InAssetStatusInfoProvider));
	SCCStatus.StatusDescription = TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateStatic(&UE::AssetDefinitionDefault::Status::GetSourceControlStatusDescription, InAssetStatusInfoProvider));
	OutStatusInfo.Add(SCCStatus);
}
#endif

namespace UE::Editor
{
	UThumbnailInfo* FindOrCreateThumbnailInfo(UObject* AssetObject, TSubclassOf<UThumbnailInfo> ThumbnailClass)
	{
		if (AssetObject && ThumbnailClass)
		{
			if (const FObjectProperty* ObjectProperty = FindFProperty<FObjectProperty>(AssetObject->GetClass(), "ThumbnailInfo"))
			{
				// Is the property marked as Instanced?
				if (ObjectProperty->HasAllPropertyFlags(CPF_PersistentInstance | CPF_ExportObject | CPF_InstancedReference))
				{
					// Get the thumbnail.
					UThumbnailInfo* ThumbnailInfo = Cast<UThumbnailInfo>(ObjectProperty->GetObjectPropertyValue_InContainer(AssetObject));
					if (ThumbnailInfo && ThumbnailInfo->GetClass() == ThumbnailClass)
					{
						return ThumbnailInfo;
					}

					// We couldn't find it, need to initialize it.
					ThumbnailInfo = NewObject<UThumbnailInfo>(AssetObject, ThumbnailClass, NAME_None, RF_Transactional);
					ObjectProperty->SetObjectPropertyValue_InContainer(AssetObject, ThumbnailInfo);

					return ThumbnailInfo;
				}
			}
		}
		
		return nullptr;
	}
}

#undef LOCTEXT_NAMESPACE
