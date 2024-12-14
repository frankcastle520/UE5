// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "Templates/UnrealTemplate.h"
#include "Widgets/Notifications/SNotificationList.h"

class UVCamPixelStreamingSubsystem;

namespace UE::PixelStreamingVCam
{
	/** Displays a toaster message to the user when a signalling server is not available */
	class FMissingSignallingServerNotifier
		: public FNoncopyable
	{
	public:

		FMissingSignallingServerNotifier(UVCamPixelStreamingSubsystem& Subsystem UE_LIFETIMEBOUND);
		~FMissingSignallingServerNotifier();

	private:

		/** System that created us */
		UVCamPixelStreamingSubsystem& OwningSubsystem;

		enum class ENotificationState : uint8
		{
			NotDisplayed,
			AwaitingUserAction,
			Displayed
		} NotificationState = ENotificationState::NotDisplayed;
		/** Whether the user set to not receive notifications about this anymore */
		bool bAreNotificationsMuted = false;

		/** The notification that was created. Used to fade it out if the server comes back online. */
		TSharedPtr<SNotificationItem> CurrentNotification;

		void DisplayNotificationIfNeeded();
		void DisplayNotification();
		
		void OnClickLaunch();
		void OnClickSkip();
		
		void CloseNotification(const FText& NewTitle, SNotificationItem::ECompletionState NewCompletionState = SNotificationItem::CS_None, const FText& Subtext = FText::GetEmpty());
	};
}


