// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AssetRegistry/AssetData.h"
#include "IContentBrowserSingleton.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "LiveLinkHub.h"
#include "LiveLinkHubLog.h"
#include "LiveLinkHubPlaybackController.h"
#include "LiveLinkHubRecordingController.h"
#include "SLiveLinkHubRecordingListView.h"
#include "LiveLinkRecording.h"
#include "Modules/ModuleManager.h"
#include "Misc/PackageName.h"

#define LOCTEXT_NAMESPACE "LiveLinkHub.RecordingList"

class FLiveLinkHubRecordingListController
{
public:
	FLiveLinkHubRecordingListController(const TSharedRef<FLiveLinkHub>& InLiveLinkHub)
		: LiveLinkHub(InLiveLinkHub)
	{
	}

	/** Create the list's widget. */
	TSharedRef<SWidget> MakeRecordingList()
	{
		return SNew(SLiveLinkHubRecordingListView)
			.OnImportRecording_Raw(this, &FLiveLinkHubRecordingListController::OnImportRecording)
			.OnEject_Raw(this, &FLiveLinkHubRecordingListController::OnEjectRecording)
			.CanEject_Raw(this, &FLiveLinkHubRecordingListController::CanEjectRecording);
	}

private:
	/** Handler called when a recording is clicked which will start the recording. */
	void OnImportRecording(const FAssetData& AssetData)
	{
		if (const TSharedPtr<FLiveLinkHub> HubPtr = LiveLinkHub.Pin())
		{
			if (HubPtr->GetRecordingController()->IsRecording())
			{
				return;
			}

			UObject* RecordingAssetData = AssetData.GetAsset();
			if (!RecordingAssetData)
			{
				UE_LOG(LogLiveLinkHub, Warning, TEXT("Failed to import recording %s"), *AssetData.AssetName.ToString());
				return;
			}

			ULiveLinkRecording* ImportedRecording = CastChecked<ULiveLinkRecording>(RecordingAssetData);
			
			if (UE::IsSavingPackage(nullptr) && !ImportedRecording->IsFullyLoaded())
			{
				// With async saving we risk triggering checks during StaticFindObjectFast, even if the package we are loading isn't the one
				// being saved. This won't occur if the recording is fully loaded into memory already.
				UE_LOG(LogLiveLinkHub, Warning, TEXT("Can't start recording because a package is saving"));
				return;
			}
			
			HubPtr->GetPlaybackController()->PreparePlayback(ImportedRecording);
		}
	}

	void OnEjectRecording()
	{
		if (const TSharedPtr<FLiveLinkHub> HubPtr = LiveLinkHub.Pin())
		{
			HubPtr->GetPlaybackController()->EjectAndUnload();
		}
	}

	bool CanEjectRecording() const
	{
		const TSharedPtr<FLiveLinkHub> HubPtr = LiveLinkHub.Pin();
		return HubPtr && HubPtr->GetPlaybackController()->GetRecording().IsValid();
	}

private:
	/** LiveLinkHub object that holds the different controllers. */
	TWeakPtr<FLiveLinkHub> LiveLinkHub;
};

#undef LOCTEXT_NAMESPACE /* LiveLinkHub.RecordingList */
