// Copyright Epic Games, Inc. All Rights Reserved.

#include "MediaPlateResourceCustomization.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "MediaPlateComponent.h"
#include "MediaPlaylist.h"
#include "MediaSource.h"
#include "PropertyCustomizationHelpers.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SFilePathPicker.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Layout/SBox.h"

#define LOCTEXT_NAMESPACE "MediaPlateResourceCustomization"

TSharedRef<IPropertyTypeCustomization> FMediaPlateResourceCustomization::MakeInstance()
{
	return MakeShared<FMediaPlateResourceCustomization>();
}

void FMediaPlateResourceCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InStructPropertyHandle, FDetailWidgetRow& InHeaderRow, IPropertyTypeCustomizationUtils& InStructCustomizationUtils)
{
	const TAttribute<EVisibility> MediaSourceFileVisibility(this, &FMediaPlateResourceCustomization::GetFileSelectorVisibility);
	const TAttribute<EVisibility> MediaSourceAssetVisibility(this, &FMediaPlateResourceCustomization::GetAssetSelectorVisibility);
	const TAttribute<EVisibility> MediaSourcePlaylistVisibility(this, &FMediaPlateResourceCustomization::GetPlaylistSelectorVisibility);

	MediaPlateResourcePropertyHandle = InStructPropertyHandle;

	void* StructRawData;
	// Try accessing the raw value, so we make sure there's only one struct edited. Multiple Access edit is currently not supported
	const FPropertyAccess::Result AccessResult = MediaPlateResourcePropertyHandle->GetValueData(StructRawData);

	TSharedPtr<SWidget> ValueWidgetContent;

	if (AccessResult == FPropertyAccess::Success)
	{
		ValueWidgetContent = SNew(SBox)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.HAlign(HAlign_Left)
			[
				SNew(SSegmentedControl<EMediaPlateResourceType>)
					.Value(this, &FMediaPlateResourceCustomization::GetAssetType)
					.OnValueChanged(this, &FMediaPlateResourceCustomization::OnAssetTypeChanged)

				+ SSegmentedControl<EMediaPlateResourceType>::Slot(EMediaPlateResourceType::External)
					.Text(LOCTEXT("File", "File"))
					.ToolTip(LOCTEXT("File_ToolTip",
						"Select this if you want to use a file path to a media file on disk."))

				+ SSegmentedControl<EMediaPlateResourceType>::Slot(EMediaPlateResourceType::Asset)
					.Text(LOCTEXT("Asset", "Asset"))
					.ToolTip(LOCTEXT("Asset_ToolTip",
						"Select this if you want to use a Media Source asset."))

				+ SSegmentedControl<EMediaPlateResourceType>::Slot(EMediaPlateResourceType::Playlist)
					.Text(LOCTEXT("Playlist", "Playlist"))
					.ToolTip(LOCTEXT("Playlist_ToolTip",
						"Select this if you want to use a Media Playlist asset."))
			]
			+ SVerticalBox::Slot()
			[
				SNew(SBox)
				.Visibility(MediaSourceAssetVisibility)
				.HAlign(HAlign_Fill)
				[
					SNew(SObjectPropertyEntryBox)
					.AllowedClass(UMediaSource::StaticClass())
					.ObjectPath(this, &FMediaPlateResourceCustomization::GetMediaAssetPath)
					.OnObjectChanged(this, &FMediaPlateResourceCustomization::OnMediaAssetChanged)
				]
			]
			+ SVerticalBox::Slot()
			[
				SNew(SBox)
				.Visibility(MediaSourceFileVisibility)
				.HAlign(HAlign_Fill)
				[
					SNew(SFilePathPicker)
					.BrowseButtonImage(FAppStyle::GetBrush("PropertyWindow.Button_Ellipsis"))
					.BrowseButtonStyle(FAppStyle::Get(), "HoverHintOnly")
					.BrowseButtonToolTip(LOCTEXT("FileButtonToolTipText", "Choose a file from this computer"))
					.BrowseTitle(LOCTEXT("PropertyEditorTitle", "File picker..."))
					.FilePath(this, &FMediaPlateResourceCustomization::GetMediaPath)
					.FileTypeFilter(TEXT("All files (*.*)|*.*"))
					.OnPathPicked(this, &FMediaPlateResourceCustomization::OnMediaPathPicked)
				]
			]
			+ SVerticalBox::Slot()
			[
				SNew(SBox)
				.Visibility(MediaSourcePlaylistVisibility)
				.HAlign(HAlign_Fill)
				[
					SNew(SObjectPropertyEntryBox)
					.AllowedClass(UMediaPlaylist::StaticClass())
					.ObjectPath(this, &FMediaPlateResourceCustomization::GetPlaylistPath)
					.OnObjectChanged(this, &FMediaPlateResourceCustomization::OnPlaylistChanged)
				]
			]
		];
	}
	else if (AccessResult == FPropertyAccess::MultipleValues)
	{
		ValueWidgetContent = SNew(STextBlock)
			.Text(LOCTEXT("MultipleValues", "Multiple Selection"))
			.ToolTipText(LOCTEXT("MultipleValues_ToolTip",
			"Multiple Media Player Resource properties selected. Select a single property to edit it."))
			.Font(IDetailLayoutBuilder::GetDetailFont());
	}
	else
	{
		ValueWidgetContent = SNew(STextBlock)
			.Text(LOCTEXT("AccessError", "Error accessing property"))
			.ToolTipText(LOCTEXT("AccessError_ToolTip",
			"Error occurred while accessing Media Player Resource property."))
			.Font(IDetailLayoutBuilder::GetDetailFont());
	}

	InHeaderRow.NameContent()
	[
		MediaPlateResourcePropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	[
		ValueWidgetContent.ToSharedRef()
	];
}

void FMediaPlateResourceCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& InChildBuilder, IPropertyTypeCustomizationUtils& InCustomizationUtils)
{
}

EMediaPlateResourceType FMediaPlateResourceCustomization::GetAssetType() const
{
	constexpr EMediaPlateResourceType DefaultType = EMediaPlateResourceType::Asset;

	if (const FMediaPlateResource* MediaPlateResource = GetMediaPlateSourcePtr())
	{
		return MediaPlateResource->GetResourceType();
	}

	return DefaultType;
}

FMediaPlateResource* FMediaPlateResourceCustomization::GetMediaPlateSourcePtr() const
{
	if (!MediaPlateResourcePropertyHandle)
	{
		return nullptr;
	}

	void* StructRawData;
	const FPropertyAccess::Result AccessResult = MediaPlateResourcePropertyHandle->GetValueData(StructRawData);
	if (AccessResult == FPropertyAccess::Success)
	{
		return static_cast<FMediaPlateResource*>(StructRawData);
	}

	return nullptr;
}

UObject* FMediaPlateResourceCustomization::GetMediaPlateResourceOwner() const
{
	TArray<UObject*> OuterObjects;
	MediaPlateResourcePropertyHandle->GetOuterObjects(OuterObjects);

	if (OuterObjects.Num() != 1)
	{
		return nullptr;
	}

	if (UObject* Outer = OuterObjects[0])
	{
		return Outer;
	}

	return nullptr;
}

void FMediaPlateResourceCustomization::OnAssetTypeChanged(EMediaPlateResourceType InMediaSourceType)
{
	if (GetAssetType() != InMediaSourceType)
	{
		// Updating value and notifying, in case the user is switching between already specified options
		if (FMediaPlateResource* MediaPlateResource = GetMediaPlateSourcePtr())
		{
			const FScopedTransaction Transaction(LOCTEXT("OnMediaSourceTypeChanged", "Media Source type changed"));

			if (UObject* MediaPlateResourceOwner = GetMediaPlateResourceOwner())
			{
				MediaPlateResourceOwner->Modify();
				MediaPlateResource->Modify();
			}

			MediaPlateResource->SetResourceType(InMediaSourceType);
			MediaPlateResourcePropertyHandle->NotifyPostChange(EPropertyChangeType::ValueSet);
		}
	}
}

FString FMediaPlateResourceCustomization::GetMediaAssetPath() const
{
	FString MediaAssetPath;

	if (const FMediaPlateResource* MediaPlateResource = GetMediaPlateSourcePtr())
	{
		return MediaPlateResource->GetMediaAsset()->GetPathName();
	}

	return MediaAssetPath;
}

FString FMediaPlateResourceCustomization::GetPlaylistPath() const
{
	FString PlaylistAssetPath;

	if (const FMediaPlateResource* MediaPlateResource = GetMediaPlateSourcePtr())
	{
		if (const UMediaPlaylist* Playlist = MediaPlateResource->GetSourcePlaylist())
		{
			PlaylistAssetPath = Playlist->GetPathName();
		}
	}

	return PlaylistAssetPath;
}

FString FMediaPlateResourceCustomization::GetMediaPath() const
{
	FString ExternalMediaPath;

	if (const FMediaPlateResource* MediaPlateResource = GetMediaPlateSourcePtr())
	{
		ExternalMediaPath = MediaPlateResource->GetExternalMediaPath();
	}

	return ExternalMediaPath;
}

void FMediaPlateResourceCustomization::OnMediaAssetChanged(const FAssetData& InAssetData)
{
	if (!MediaPlateResourcePropertyHandle)
	{
		return;
	}

	if (const UMediaSource* MediaAsset = Cast<UMediaSource>(InAssetData.GetAsset()))
	{
		if (FMediaPlateResource* MediaPlateResource = GetMediaPlateSourcePtr())
		{
			if (UObject* MediaPlateResourceOwner = GetMediaPlateResourceOwner())
			{
				const FScopedTransaction Transaction(LOCTEXT("OnMediaSourceAssetChanged", "Media Source asset changed"));
				MediaPlateResourceOwner->Modify();
				MediaPlateResource->Modify();

				MediaPlateResource->SelectAsset(MediaAsset, MediaPlateResourceOwner);
				MediaPlateResourcePropertyHandle->NotifyPostChange(EPropertyChangeType::ValueSet);
			}
		}
	}
}

void FMediaPlateResourceCustomization::OnMediaPathPicked(const FString& InPickedPath)
{
	if (!MediaPlateResourcePropertyHandle)
	{
		return;
	}

	FMediaPlateResource* MediaPlateResource = GetMediaPlateSourcePtr();

	if (!MediaPlateResource)
	{
		return;
	}

	if (!InPickedPath.IsEmpty() && InPickedPath != MediaPlateResource->GetExternalMediaPath())
	{
		const FScopedTransaction Transaction(LOCTEXT("OnMediaExternalPathChanged", "Media external file path changed"));
		if (UObject* MediaPlateResourceOwner = GetMediaPlateResourceOwner())
		{
			MediaPlateResourceOwner->Modify();
			MediaPlateResource->Modify();

			MediaPlateResource->LoadExternalMedia(InPickedPath, MediaPlateResourceOwner);
			MediaPlateResourcePropertyHandle->NotifyPostChange(EPropertyChangeType::ValueSet);
		}
	}
}

void FMediaPlateResourceCustomization::OnPlaylistChanged(const FAssetData& InAssetData)
{
	if (!MediaPlateResourcePropertyHandle)
	{
		return;
	}

	const UMediaPlaylist* Playlist = Cast<UMediaPlaylist>(InAssetData.GetAsset());
	if (FMediaPlateResource* MediaPlateResource = GetMediaPlateSourcePtr())
	{
		const FScopedTransaction Transaction(LOCTEXT("OnPlaylistChanged", "Media Playlist changed"));
		if (UObject* MediaPlateResourceOwner = GetMediaPlateResourceOwner())
		{
			MediaPlateResourceOwner->Modify();
		}

		// No need to call MediaPlateResource->Modify():
		// ActivePlaylist property will be overwritten, not modified

		MediaPlateResource->SelectPlaylist(Playlist);
		MediaPlateResourcePropertyHandle->NotifyPostChange(EPropertyChangeType::ValueSet);
	}
}

EVisibility FMediaPlateResourceCustomization::GetAssetSelectorVisibility() const
{
	return GetAssetType() == EMediaPlateResourceType::Asset ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility FMediaPlateResourceCustomization::GetFileSelectorVisibility() const
{
	return GetAssetType() == EMediaPlateResourceType::External ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility FMediaPlateResourceCustomization::GetPlaylistSelectorVisibility() const
{
	return GetAssetType() == EMediaPlateResourceType::Playlist ? EVisibility::Visible : EVisibility::Collapsed;
}

#undef LOCTEXT_NAMESPACE
