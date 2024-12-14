// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetToolsModule.h"
#include "ContentBrowserModule.h"
#include "Delegates/DelegateCombinations.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/PlatformProcess.h"
#include "IContentBrowserSingleton.h"
#include "Implementations/LiveLinkUAssetRecording.h"
#include "LiveLinkHubModule.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Recording/LiveLinkHubPlaybackController.h"
#include "Recording/LiveLinkRecording.h"
#include "UObject/SavePackage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Text/STextBlock.h"


#define LOCTEXT_NAMESPACE "LiveLinkHub.RecordingListView"

class SLiveLinkHubRecordingListView : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnImportRecording, const struct FAssetData&);
	DECLARE_DELEGATE(FOnEject);
	DECLARE_DELEGATE_RetVal(bool, FCanEject)
	
	SLATE_BEGIN_ARGS(SLiveLinkHubRecordingListView)
		{}
		SLATE_EVENT(FOnImportRecording, OnImportRecording)
		SLATE_EVENT(FOnEject, OnEject)
		SLATE_EVENT(FCanEject, CanEject)
	SLATE_END_ARGS()

	SLiveLinkHubRecordingListView()
	{
		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		OnAssetAddedHandle = AssetRegistry.OnAssetAdded().AddRaw(this, &SLiveLinkHubRecordingListView::OnAssetAdded);
		OnAssetRemovedHandle = AssetRegistry.OnAssetRemoved().AddRaw(this, &SLiveLinkHubRecordingListView::OnAssetRemoved);
	}

	virtual ~SLiveLinkHubRecordingListView() override
	{
		if (FModuleManager::Get().IsModuleLoaded("AssetRegistry"))
		{
			IAssetRegistry& AssetRegistry = FModuleManager::GetModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
			if (OnAssetAddedHandle.IsValid())
			{
				AssetRegistry.OnAssetAdded().Remove(OnAssetAddedHandle);
			}
			if (OnAssetRemovedHandle.IsValid())
			{
				AssetRegistry.OnAssetRemoved().Remove(OnAssetRemovedHandle);
			}
		}
	}
	
	//~ Begin SWidget interface
	void Construct(const FArguments& InArgs)
	{
		OnImportRecordingDelegate = InArgs._OnImportRecording;
		OnEjectDelegate = InArgs._OnEject;
		OnCanEjectDelegate = InArgs._CanEject;
		
		ChildSlot
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Center)
				.Padding(4.f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("RecordingTitle", "Recordings"))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				.Padding(4.f)
				[
					SNew(SButton)
					.Visibility(this, &SLiveLinkHubRecordingListView::GetRecordingPickerVisibility)
					.Text(LOCTEXT("EjectButton", "Exit Playback"))
					.OnClicked(this, &SLiveLinkHubRecordingListView::OnEjectClicked)
					.IsEnabled(this, &SLiveLinkHubRecordingListView::CanEjectRecording)
				]
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.VAlign(VAlign_Fill)
				[
					SNew(SBox)
					.Visibility(this, &SLiveLinkHubRecordingListView::GetRecordingPickerVisibility)
					[
						CreateRecordingPicker()
					]
				]
				+SVerticalBox::Slot()
				.AutoHeight()
				.VAlign(VAlign_Top)
				.HAlign(HAlign_Center)
				[
					SNew(STextBlock)
					.Visibility_Lambda([this]()
					{
						EVisibility RecordingPickerVisibility = GetRecordingPickerVisibility();
						return RecordingPickerVisibility == EVisibility::Visible ? EVisibility::Collapsed : EVisibility::Visible;
					})
					.Text(GetNoAssetsWarningText())
				]
			]
		];
	}
	//~ End SWidget interface

private:
	/** Callback to notice the hub that we've selected a recording to play. */
	void OnImportRecording(const FAssetData& AssetData) const
	{
		OnImportRecordingDelegate.Execute(AssetData);
	}

	FReply OnEjectClicked()
	{
		OnEjectDelegate.ExecuteIfBound();
		return FReply::Handled();
	}

	bool CanEjectRecording() const
	{
		check(OnCanEjectDelegate.IsBound());
		return OnCanEjectDelegate.Execute();
	}

	/** When an asset is added to the asset registry. */
	void OnAssetAdded(const FAssetData& InAssetData)
	{
		if (InAssetData.IsValid()
			&& (InAssetData.AssetClassPath == ULiveLinkUAssetRecording::StaticClass()->GetClassPathName()
				|| InAssetData.AssetClassPath == ULiveLinkRecording::StaticClass()->GetClassPathName()))
		{
			bAssetsAvailableCached = true;
		}
	}

	/** When an asset is removed from the asset registry. */
	void OnAssetRemoved(const FAssetData& InAssetData)
	{
		if (InAssetData.IsValid()
			&& (InAssetData.AssetClassPath == ULiveLinkUAssetRecording::StaticClass()->GetClassPathName()
				|| InAssetData.AssetClassPath == ULiveLinkRecording::StaticClass()->GetClassPathName()))
		{
			// Let the cache recalculate.
			bAssetsAvailableCached.Reset();
		}
	}

	/** The visibility status of the recording picker. */
	EVisibility GetRecordingPickerVisibility() const
	{
		// Cache the value initially, otherwise it is set on the asset added event.
		if (!bAssetsAvailableCached.IsSet())
		{
			FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
			TArray<FAssetData> AssetDataArray;
			FARFilter Filter = MakeAssetFilter();
			AssetRegistryModule.Get().GetAssets(MoveTemp(Filter), AssetDataArray);

			bAssetsAvailableCached = AssetDataArray.Num() > 0;
		}
		
		return bAssetsAvailableCached.GetValue() ? EVisibility::Visible : EVisibility::Hidden;
	}

	/** Creates the asset picker widget for selecting a recording. */
	TSharedRef<SWidget> CreateRecordingPicker()
	{
		FMenuBuilder MenuBuilder(true, nullptr);

		IContentBrowserSingleton& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get();
		
		FAssetPickerConfig AssetPickerConfig;
		{
			AssetPickerConfig.SelectionMode = ESelectionMode::Single;
			AssetPickerConfig.InitialAssetViewType = EAssetViewType::Column;
			AssetPickerConfig.bFocusSearchBoxWhenOpened = true;
			AssetPickerConfig.bAllowNullSelection = false;
			AssetPickerConfig.bShowBottomToolbar = true;
			AssetPickerConfig.bAutohideSearchBar = false;
			AssetPickerConfig.bAllowDragging = false;
			AssetPickerConfig.bCanShowClasses = false;
			AssetPickerConfig.bShowPathInColumnView = true;
			AssetPickerConfig.bSortByPathInColumnView = false;
			AssetPickerConfig.AssetShowWarningText = GetNoAssetsWarningText();

			AssetPickerConfig.bForceShowEngineContent = true;
			AssetPickerConfig.bForceShowPluginContent = true;

			AssetPickerConfig.Filter = MakeAssetFilter();
			AssetPickerConfig.OnAssetDoubleClicked = FOnAssetSelected::CreateRaw(this, &SLiveLinkHubRecordingListView::OnImportRecording);
			AssetPickerConfig.OnGetAssetContextMenu = FOnGetAssetContextMenu::CreateRaw(this, &SLiveLinkHubRecordingListView::GetAssetContextMenu);
		}

		MenuBuilder.BeginSection(NAME_None, LOCTEXT("ImportRecording_MenuSection", "Import Recording"));
		{
			AssetPicker = ContentBrowser.CreateAssetPicker(AssetPickerConfig);
			TSharedRef<SWidget> PresetPicker = SNew(SBox)
			.VAlign(VAlign_Fill)
			.HAlign(HAlign_Fill)
			[
				AssetPicker.ToSharedRef()
			];

			MenuBuilder.AddWidget(PresetPicker, FText(), true, false);
		}
		MenuBuilder.EndSection();

		return MenuBuilder.MakeWidget();
	}

	/** Create a filter for available recording assets. */
	FARFilter MakeAssetFilter() const
	{
		FARFilter Filter;
		Filter.ClassPaths.Add(ULiveLinkRecording::StaticClass()->GetClassPathName());
		Filter.bRecursiveClasses = true;
		Filter.bRecursivePaths = true;
		// There shouldn't be recordings that exist in memory but not on disk. Necessary to properly register deleted assets.
		Filter.bIncludeOnlyOnDiskAssets = true;
		return Filter;
	}

	TSharedPtr<SWidget> GetAssetContextMenu(const TArray<FAssetData>& SelectedAssets) const
	{
		if (SelectedAssets.Num() <= 0)
		{
			return nullptr;
		}

		TWeakObjectPtr<UObject> SelectedAsset = SelectedAssets[0].GetAsset();
		if (!SelectedAsset.IsValid())
		{
			return nullptr;
		}

		FMenuBuilder MenuBuilder(true, MakeShared<FUICommandList>());

		MenuBuilder.BeginSection(TEXT("Recording"), LOCTEXT("RecordingSectionLabel", "Recording"));
		{
			MenuBuilder.AddMenuEntry(
				LOCTEXT("RenameRecordingLabel", "Rename"),
				LOCTEXT("RenameRecordingTooltip", "Rename the recording"),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "GenericCommands.Rename"),
				FUIAction(
					FExecuteAction::CreateLambda([SelectedAsset, this] ()
					{
						if (SelectedAsset.IsValid())
						{
							const FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
							ContentBrowserModule.Get().ExecuteRename(AssetPicker);
						}
					}),
					FCanExecuteAction::CreateLambda([] () { return true; })
				)
			);
			
			MenuBuilder.AddMenuEntry(
				LOCTEXT("DuplicateRecordingLabel", "Duplicate"),
				LOCTEXT("DuplicateRecordingTooltip", "Duplicate the recording"),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "GenericCommands.Duplicate"),
				FUIAction(
					FExecuteAction::CreateLambda([SelectedAsset] ()
					{
						if (SelectedAsset.IsValid())
						{
							IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();
							
							FString TargetName;
							FString TargetPackageName;
							IAssetTools::Get().CreateUniqueAssetName(SelectedAsset->GetOutermost()->GetName(), TEXT("_Copy"), TargetPackageName, TargetName);

							// Duplicate the asset.
							UObject* NewAsset = AssetTools.DuplicateAsset(TargetName, FPackageName::GetLongPackagePath(TargetPackageName), SelectedAsset.Get());
							FSavePackageArgs SavePackageArgs;
							SavePackageArgs.TopLevelFlags = RF_Public | RF_Standalone;
							SavePackageArgs.Error = GLog;

							// Save the package.
							const FString PackageFileName = FPackageName::LongPackageNameToFilename(TargetPackageName, FPackageName::GetAssetPackageExtension());
							UPackage::SavePackage(NewAsset->GetPackage(), NewAsset, *PackageFileName, MoveTemp(SavePackageArgs));

							// Unload the source recording data, as the bulk data would have been fully loaded to duplicate.
							const FLiveLinkHubModule& LiveLinkHubModule = FModuleManager::Get().GetModuleChecked<FLiveLinkHubModule>("LiveLinkHub");
							const TStrongObjectPtr<ULiveLinkRecording> PlaybackRecording = LiveLinkHubModule.GetPlaybackController()->GetRecording();
							if (PlaybackRecording.Get() != SelectedAsset)
							{
								CastChecked<ULiveLinkUAssetRecording>(SelectedAsset)->UnloadRecordingData();
							}
						}
					}),
					FCanExecuteAction::CreateLambda([] () { return true; })
				)
			);

			MenuBuilder.AddMenuEntry(
				LOCTEXT("OpenFileLocationLabel", "Open File Location..."),
				LOCTEXT("OpenFileLocationTooltip", "Open the folder containing this file"),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.FolderOpen"),
				FUIAction(
					FExecuteAction::CreateLambda([SelectedAsset] ()
					{
						if (SelectedAsset.IsValid())
						{
							const FString PackageName = SelectedAsset->GetPathName();
							const FString AssetFilePath = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
							const FString AbsoluteFilePath = FPaths::ConvertRelativePathToFull(AssetFilePath);
							const FString AssetDirectory = FPaths::GetPath(AbsoluteFilePath);
						
							FPlatformProcess::ExploreFolder(*AssetDirectory);
						}
					}),
					FCanExecuteAction::CreateLambda([SelectedAsset] () { return SelectedAsset.IsValid(); })
				)
			);
		}
		MenuBuilder.EndSection();

		return MenuBuilder.MakeWidget();
	}
	
	/** The text to display when no assets are found. */
	static FText GetNoAssetsWarningText()
	{
		return LOCTEXT("NoRecordings_Warning", "No Recordings Found");
	}
	
private:
	/** Delegate used for noticing the hub that a recording was selected for playback. */
	FOnImportRecording OnImportRecordingDelegate;
	/** Delegate used when ejecting a recording. */
	FOnEject OnEjectDelegate;
	/** Delegate used to determine a recording can be ejected. */
	FCanEject OnCanEjectDelegate;
	/** The asset picker used for selecting recordings. */
	TSharedPtr<SWidget> AssetPicker;
	
	/** Handle for when an asset is added to the asset registry. */
	FDelegateHandle OnAssetAddedHandle;
	/** Handle for when an asset is removed from the asset registry. */
	FDelegateHandle OnAssetRemovedHandle;
	/** True if there are recording assets that exist. */
	mutable TOptional<bool> bAssetsAvailableCached;
};

#undef LOCTEXT_NAMESPACE /* LiveLinkHub.RecordingListView */
