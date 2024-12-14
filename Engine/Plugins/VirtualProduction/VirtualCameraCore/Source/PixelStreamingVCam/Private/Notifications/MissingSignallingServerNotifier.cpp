// Copyright Epic Games, Inc. All Rights Reserved.

#include "MissingSignallingServerNotifier.h"

#include "ILevelEditor.h"
#include "BuiltinProviders/VCamPixelStreamingSession.h"
#include "IPixelStreamingEditorModule.h"
#include "LevelEditor.h"
#include "VCamPixelStreamingSubsystem.h"
#include "Editor/SceneOutliner/Public/ActorTreeItem.h"
#include "Editor/SceneOutliner/Public/ISceneOutliner.h"

#include "Framework/Notifications/NotificationManager.h"
#include "Misc/CoreDelegates.h"
#include "Stats/Stats2.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "FMissingSignallingServerNotifier"

namespace UE::PixelStreamingVCam
{
	namespace Private
	{
		static void ShowActorsInOutliner(TArray<TWeakObjectPtr<AActor>> Actors)
		{
			TWeakPtr<ILevelEditor> WeakLevelEditor = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetLevelEditorInstance();
			const TSharedPtr<ILevelEditor> LevelEditorPin = WeakLevelEditor.Pin();
			const  TSharedPtr<ISceneOutliner> SceneOutliner = LevelEditorPin ? LevelEditorPin->GetMostRecentlyUsedSceneOutliner() : nullptr;
			if (SceneOutliner)
			{
				ISceneOutlinerTreeItem* FirstItem = nullptr;
				SceneOutliner->SetSelection([&Actors, &FirstItem](ISceneOutlinerTreeItem& Item)
				{
					FirstItem = &Item;
					FActorTreeItem* ActorItem = Item.CastTo<FActorTreeItem>();
					return ActorItem && Actors.Contains(ActorItem->Actor);
				});

				if (FirstItem)
				{
					SceneOutliner->FrameItem(FirstItem->GetID());
				}
			}
		}
	}
	
	FMissingSignallingServerNotifier::FMissingSignallingServerNotifier(UVCamPixelStreamingSubsystem& Subsystem)
		: OwningSubsystem(Subsystem)
	{
		FCoreDelegates::OnEndFrame.AddRaw(this, &FMissingSignallingServerNotifier::DisplayNotificationIfNeeded);
	}

	FMissingSignallingServerNotifier::~FMissingSignallingServerNotifier()
	{
		FCoreDelegates::OnEndFrame.RemoveAll(this);
	}

	void FMissingSignallingServerNotifier::DisplayNotificationIfNeeded()
	{
		if (bAreNotificationsMuted)
		{
			return;
		}

		IPixelStreamingEditorModule& Module = IPixelStreamingEditorModule::Get();
		// There's currently no API to detect whether an external signalling server is connected. No notifications for this case.
		if (Module.UseExternalSignallingServer())
		{
			return;
		}
		
		const TSharedPtr<PixelStreamingServers::IServer> SignallingServer = Module.GetSignallingServer();
		const bool bIsServerAvailable = SignallingServer.IsValid();

		// If the server comes back up, close the notification as to not confuse the user.
		const bool bIsNotificationShown = NotificationState == ENotificationState::AwaitingUserAction && ensure(CurrentNotification);
		if (bIsNotificationShown && bIsServerAvailable)
		{
			CloseNotification(LOCTEXT("ExternallyLaunched.Title", "Server connected"), SNotificationItem::CS_Success, LOCTEXT("ExternallyLaunched.Subtext", "Local server instance detected"));
			return;
		}

		// If the user pressed skip, and the user becomes available again, reset the state... should we lose the server again, show a new notification.
		if (NotificationState == ENotificationState::Displayed)
		{
			NotificationState = bIsServerAvailable ? ENotificationState::NotDisplayed : NotificationState;
			return;
		}

		const TArray<TWeakObjectPtr<UVCamPixelStreamingSession>>& RegisteredSessions = OwningSubsystem.GetRegisteredSessions();
		const bool bShouldNotify = !bIsNotificationShown && !bIsServerAvailable && !RegisteredSessions.IsEmpty();
		if (bShouldNotify)
		{
			DisplayNotification();
		}
	}

	void FMissingSignallingServerNotifier::DisplayNotification()
	{
		NotificationState = ENotificationState::AwaitingUserAction;
		
		TArray<TWeakObjectPtr<AActor>> SessionActors;
		Algo::TransformIf(OwningSubsystem.GetRegisteredSessions(), SessionActors,
			[](const TWeakObjectPtr<UVCamPixelStreamingSession>& WeakSession){ return WeakSession.IsValid(); },
			[](const TWeakObjectPtr<UVCamPixelStreamingSession>& WeakSession)
			{
				const UVCamPixelStreamingSession* Session = WeakSession.Get();
				return Session->GetTypedOuter<AActor>();
			});
		TArray<FString> DisplayNames;
		Algo::TransformIf(SessionActors, DisplayNames,
			[](const TWeakObjectPtr<AActor>& Actor){ return Actor != nullptr; },
			[](const TWeakObjectPtr<AActor>& Actor){  return Actor->GetActorNameOrLabel(); }
			);
		const FString ClientList = FString::Join(DisplayNames, TEXT(", "));

		const FNotificationButtonInfo::FVisibilityDelegate VisibilityDelegate =  FNotificationButtonInfo::FVisibilityDelegate::CreateLambda(
			[this](auto){ return CurrentNotification.IsValid() ? EVisibility::Visible : EVisibility::Collapsed; }
			);
		FNotificationInfo NotificationInfo(LOCTEXT("SignallingServer.Title", "Signalling Server required"));
		NotificationInfo.Hyperlink.BindStatic(&Private::ShowActorsInOutliner, SessionActors);
		NotificationInfo.HyperlinkText = FText::Format(LOCTEXT("SelectActors", "Select in outliner: {0}"), FText::FromString(ClientList));
		NotificationInfo.CheckBoxState = TAttribute<ECheckBoxState>::CreateLambda(
			[this](){ return bAreNotificationsMuted ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }
			);
		NotificationInfo.CheckBoxStateChanged.BindLambda(
			[this](ECheckBoxState NewState){ bAreNotificationsMuted = NewState == ECheckBoxState::Checked; }
			);
		NotificationInfo.CheckBoxText = LOCTEXT("StopShowing", "Do not remind again until next restart");
		NotificationInfo.bFireAndForget = false;
		NotificationInfo.FadeOutDuration = 4.f;
		NotificationInfo.SubText = LOCTEXT("SignallingServer.SubTextFmt", "Some actors require a signalling server");
		NotificationInfo.ButtonDetails =
		{
			FNotificationButtonInfo(
				LOCTEXT("Launch.Label", "Launch"),
				LOCTEXT("Launch.ToolTip", "Launches a local signalling server"),
				FSimpleDelegate::CreateRaw(this, &FMissingSignallingServerNotifier::OnClickLaunch),
				VisibilityDelegate
				),
			FNotificationButtonInfo(
				LOCTEXT("Skip.Label", "Skip"),
				LOCTEXT("Skip.ToolTip", "Do nothing about this"),
				FSimpleDelegate::CreateRaw(this, &FMissingSignallingServerNotifier::OnClickSkip),
				VisibilityDelegate
				),
		};
		CurrentNotification = FSlateNotificationManager::Get().AddNotification(NotificationInfo);
	}

	void FMissingSignallingServerNotifier::OnClickLaunch()
	{
		IPixelStreamingEditorModule::Get().StartSignalling();
		CloseNotification(
			LOCTEXT("Launched", "Launched signalling server"),
			SNotificationItem::CS_Success
			);
	}

	void FMissingSignallingServerNotifier::OnClickSkip()
	{
		CloseNotification(
			LOCTEXT("Skipped", "No action taken")
			);
	}

	void FMissingSignallingServerNotifier::CloseNotification(
		const FText& NewTitle,
		SNotificationItem::ECompletionState NewCompletionState,
		const FText& Subtext
		)
	{
		if (!ensure(CurrentNotification))
		{
			return;
		}
		NotificationState = ENotificationState::Displayed;

		CurrentNotification->SetCompletionState(NewCompletionState);
		CurrentNotification->SetHyperlink({});
		CurrentNotification->SetText(NewTitle);
		CurrentNotification->SetSubText(Subtext);
		CurrentNotification->Fadeout();
		CurrentNotification.Reset();
	}
}

#undef LOCTEXT_NAMESPACE