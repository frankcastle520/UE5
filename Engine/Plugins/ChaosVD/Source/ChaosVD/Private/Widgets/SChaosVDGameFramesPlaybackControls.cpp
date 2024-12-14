// Copyright Epic Games, Inc. All Rights Reserved.

#include "SChaosVDGameFramesPlaybackControls.h"

void SChaosVDGameFramesPlaybackControls::Construct(const FArguments& InArgs, const TWeakPtr<FChaosVDPlaybackController>& InPlaybackController)
{
	this->ChildSlot
	[
		SAssignNew(FramesTimelineWidget, SChaosVDTimelineWidget)
			.IsEnabled_Raw(this, &SChaosVDGameFramesPlaybackControls::CanPlayback)
			.IsPlaying_Raw(this, &SChaosVDGameFramesPlaybackControls::IsPlaying)
			.ButtonVisibilityFlags(EChaosVDTimelineElementIDFlags::AllPlayback)
			.OnFrameChanged_Raw(this, &SChaosVDGameFramesPlaybackControls::OnFrameSelectionUpdated)
			.OnButtonClicked(this, &SChaosVDGameFramesPlaybackControls::HandleFramePlaybackButtonClicked)
			.MinFrames_Raw(this, &SChaosVDGameFramesPlaybackControls::GetMinFrames)
			.MaxFrames_Raw(this, &SChaosVDGameFramesPlaybackControls::GetMaxFrames)
			.CurrentFrame_Raw(this, &SChaosVDGameFramesPlaybackControls::GetCurrentFrame)
			.ButtonEnabledFlags_Raw(this, &SChaosVDGameFramesPlaybackControls::GetElementEnabledFlags)
	];

	RegisterNewController(InPlaybackController);
}

void SChaosVDGameFramesPlaybackControls::OnFrameSelectionUpdated(int32 NewFrameIndex)
{
	if (const TSharedPtr<FChaosVDPlaybackController> PlaybackControllerPtr = PlaybackController.Pin())
	{
		constexpr int32 SolverStage = 0;
		PlaybackControllerPtr->GoToTrackFrameAndSync(GetInstigatorID(), EChaosVDTrackType::Game, FChaosVDPlaybackController::GameTrackID, NewFrameIndex, SolverStage);
	}
}

void SChaosVDGameFramesPlaybackControls::RegisterNewController(TWeakPtr<FChaosVDPlaybackController> NewController)
{
	FChaosVDPlaybackControllerObserver::RegisterNewController(NewController);

	if (const TSharedPtr<FChaosVDPlaybackController> PlaybackControllerPtr = PlaybackController.Pin())
	{
		if (TSharedPtr<FChaosVDTrackInfo> GameTrackInfoPtr = PlaybackControllerPtr->GetMutableTrackInfo(EChaosVDTrackType::Game, FChaosVDPlaybackController::GameTrackID))
		{
			GameTrackInfoRef = GameTrackInfoPtr.ToSharedRef();
		}
	}
}

void SChaosVDGameFramesPlaybackControls::HandleFramePlaybackButtonClicked(EChaosVDPlaybackButtonsID ButtonID)
{
	if (const TSharedPtr<FChaosVDPlaybackController> PlaybackControllerPtr = PlaybackController.Pin())
	{
		PlaybackControllerPtr->HandleFramePlaybackControlInput(ButtonID, GameTrackInfoRef, GetInstigatorID());
	}
}

void SChaosVDGameFramesPlaybackControls::HandlePlaybackControllerDataUpdated(TWeakPtr<FChaosVDPlaybackController> InController)
{
	if (const TSharedPtr<FChaosVDPlaybackController> PlaybackControllerPtr = PlaybackController.Pin())
	{
		if (TSharedPtr<FChaosVDTrackInfo> GameTrackInfoPtr = PlaybackControllerPtr->GetMutableTrackInfo(EChaosVDTrackType::Game, FChaosVDPlaybackController::GameTrackID))
		{
			GameTrackInfoRef = GameTrackInfoPtr.ToSharedRef();
		}
	}
}

bool SChaosVDGameFramesPlaybackControls::CanPlayback() const
{
	bool bCanControlPlayback = false;
	if (const TSharedPtr<FChaosVDPlaybackController> PlaybackControllerPtr = PlaybackController.Pin())
	{
		if (PlaybackControllerPtr->IsRecordingLoaded() && GameTrackInfoRef->TrackType != EChaosVDTrackType::Invalid)
		{
			// GameFrames playback controls can only be enabled on the Recorded timestamp sync mode
			bool bIsCompatibleSyncMode = true;

			if (PlaybackControllerPtr->GetTimelineSyncMode() == EChaosVDSyncTimelinesMode::NetworkTick)
			{
				static TArray<TSharedPtr<const FChaosVDTrackInfo>> OutTrackInfo;
				OutTrackInfo.Reset();

				bool bHasTracksWithNetworkSyncData = false;
				PlaybackControllerPtr->GetAvailableTrackInfosAtTrackFrame(EChaosVDTrackType::Solver, GameTrackInfoRef, OutTrackInfo);
				for (const TSharedPtr<const FChaosVDTrackInfo>& TrackInfo : OutTrackInfo)
				{
					if (TrackInfo && TrackInfo->bHasNetworkSyncData)
					{
						bHasTracksWithNetworkSyncData = true;
						break;
					}
				}

				// If we are on Network tick sync mode, but we don't have ant solver track that supports that mode loaded yet (or ever if this was an old file or recorded a standalone game)
				// allow the game track playback control to work. The solver track widgets themselves will show the corresponding compatibility warning
				bIsCompatibleSyncMode &= !bHasTracksWithNetworkSyncData;
			}

			TSharedPtr<FChaosVDTrackInfo> CurrentTrackBeingPlayed = PlaybackControllerPtr->GetCurrentPlayingTrackInfo();
			bCanControlPlayback = (!CurrentTrackBeingPlayed || (CurrentTrackBeingPlayed && CurrentTrackBeingPlayed->TrackType == EChaosVDTrackType::Game)) && bIsCompatibleSyncMode;
		}
	}

	return bCanControlPlayback;
}

bool SChaosVDGameFramesPlaybackControls::IsPlaying() const
{
	return GameTrackInfoRef->bIsPlaying;
}

int32 SChaosVDGameFramesPlaybackControls::GetCurrentFrame() const
{
	return GameTrackInfoRef->CurrentFrame >= 0 ? GameTrackInfoRef->CurrentFrame : 0 ;
}

int32 SChaosVDGameFramesPlaybackControls::GetMinFrames() const
{
	return 0;
}

int32 SChaosVDGameFramesPlaybackControls::GetMaxFrames() const
{
	return GameTrackInfoRef->MaxFrames > 0 ? GameTrackInfoRef->MaxFrames - 1 : 0;
}

EChaosVDTimelineElementIDFlags SChaosVDGameFramesPlaybackControls::GetElementEnabledFlags() const
{
	EChaosVDTimelineElementIDFlags EnabledButtonFlags = EChaosVDTimelineElementIDFlags::All;
	if (const TSharedPtr<FChaosVDPlaybackController> ControllerSharedPtr = PlaybackController.Pin())
	{
		if (ControllerSharedPtr->IsPlayingLiveSession())
		{
			constexpr EChaosVDTimelineElementIDFlags PlaybackElementDisabledDuringLiveSession = EChaosVDTimelineElementIDFlags::Stop | EChaosVDTimelineElementIDFlags::Next | EChaosVDTimelineElementIDFlags::Prev;
			EnumRemoveFlags(EnabledButtonFlags, PlaybackElementDisabledDuringLiveSession);
			return EnabledButtonFlags;
		}
		else
		{
			return EnabledButtonFlags;
		}
	}

	return EnabledButtonFlags;
}
