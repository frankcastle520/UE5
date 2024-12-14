// Copyright Epic Games, Inc. All Rights Reserved.

#include "AvaMediaSettings.h"
#include "IAvaMediaModule.h"
#include "Misc/MessageDialog.h"

#define LOCTEXT_NAMESPACE "AvaMediaSettings"

const FName UAvaMediaSettings::SynchronizedEventsFeatureSelection_Default(TEXT("Default"));

UAvaMediaSettings::UAvaMediaSettings()
{
	CategoryName = TEXT("Motion Design");
	SectionName  = TEXT("Playback & Broadcast");
	PlayableSettings.SynchronizedEventsFeature.Implementation = SynchronizedEventsFeatureSelection_Default.ToString();
}

UAvaMediaSettings* UAvaMediaSettings::GetSingletonInstance()
{
	UAvaMediaSettings* DefaultSettings = GetMutableDefault<UAvaMediaSettings>();
	static bool bInitialized = false;
	if (!bInitialized)
	{
		bInitialized = true;
		DefaultSettings->SetFlags(RF_Transactional);
	}
	return DefaultSettings;
}

ELogVerbosity::Type UAvaMediaSettings::ToLogVerbosity(EAvaMediaLogVerbosity InAvaMediaLogVerbosity)
{
	switch (InAvaMediaLogVerbosity)
	{
	case EAvaMediaLogVerbosity::NoLogging:
		return ELogVerbosity::NoLogging;
	case EAvaMediaLogVerbosity::Fatal:
		return ELogVerbosity::Fatal;
	case EAvaMediaLogVerbosity::Error:
		return ELogVerbosity::Error;
	case EAvaMediaLogVerbosity::Warning:
		return ELogVerbosity::Warning;
	case EAvaMediaLogVerbosity::Display:
		return ELogVerbosity::Display;
	case EAvaMediaLogVerbosity::Log:
		return ELogVerbosity::Log;
	case EAvaMediaLogVerbosity::Verbose:
		return ELogVerbosity::Verbose;
	case EAvaMediaLogVerbosity::VeryVerbose:
		return ELogVerbosity::VeryVerbose;
	default:
		return ELogVerbosity::NoLogging;
	}
}

#if WITH_EDITOR
void UAvaMediaSettings::PostEditChangeProperty(FPropertyChangedEvent& InPropertyChangedEvent)
{
	Super::PostEditChangeProperty(InPropertyChangedEvent);

	// Check if playback client is auto-start.
	static const FName AutoStartPlaybackClientPropertyName = GET_MEMBER_NAME_CHECKED(UAvaMediaSettings, bAutoStartPlaybackClient);

	if (InPropertyChangedEvent.MemberProperty->GetFName() == AutoStartPlaybackClientPropertyName)
	{
		IAvaMediaModule& AvaMediaModule = IAvaMediaModule::Get();
		if (bAutoStartPlaybackClient && !AvaMediaModule.IsPlaybackClientStarted())
		{
			const FText MessageText = LOCTEXT("StartPlaybackClientQuestion", "Do you want to start the playback client now?");
			const EAppReturnType::Type Reply = FMessageDialog::Open(EAppMsgType::YesNo, EAppReturnType::Yes, MessageText);

			if (Reply == EAppReturnType::Yes)
			{
				AvaMediaModule.StartPlaybackClient();
			}
		}
		else if (!bAutoStartPlaybackClient && AvaMediaModule.IsPlaybackClientStarted())
		{
			const FText MessageText = LOCTEXT("StopPlaybackClientQuestion", "Playback Client is currently running. Do you want to stop it now?");
			const EAppReturnType::Type Reply = FMessageDialog::Open(EAppMsgType::YesNo, EAppReturnType::Yes, MessageText);

			if (Reply == EAppReturnType::Yes)
			{
				AvaMediaModule.StopPlaybackClient();
			}
		}
	}
}
#endif

#undef LOCTEXT_NAMESPACE
