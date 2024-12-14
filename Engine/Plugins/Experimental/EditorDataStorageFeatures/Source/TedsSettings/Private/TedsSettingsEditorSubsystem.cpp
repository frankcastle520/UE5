// Copyright Epic Games, Inc. All Rights Reserved.

#include "TedsSettingsEditorSubsystem.h"

#include "Editor.h"
#include "TedsSettingsLog.h"
#include "TedsSettingsManager.h"

namespace UE::Editor::Settings::Private
{
	static TAutoConsoleVariable<bool> CVarTedsSettingsEnable(
		TEXT("TEDS.Feature.Settings.Enable"),
		false,
		TEXT("When true, settings objects from the ISettingsModule will be mirrored to rows in the editor data storage."));
} // namespace UE::Editor::Settings::Private

UTedsSettingsEditorSubsystem::UTedsSettingsEditorSubsystem()
	: UEditorSubsystem()
	, SettingsManager{ MakeShared<FTedsSettingsManager>() }
	, EnabledChangedDelegate{ }
{
}

const bool UTedsSettingsEditorSubsystem::IsEnabled() const
{
	return UE::Editor::Settings::Private::CVarTedsSettingsEnable.GetValueOnGameThread();
}

UTedsSettingsEditorSubsystem::FOnEnabledChanged& UTedsSettingsEditorSubsystem::OnEnabledChanged()
{
	return EnabledChangedDelegate;
}

void UTedsSettingsEditorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	UE_LOG(LogTedsSettings, Log, TEXT("UTedsSettingsEditorSubsystem::Initialize"));

	UE::Editor::Settings::Private::CVarTedsSettingsEnable->SetOnChangedCallback(
		FConsoleVariableDelegate::CreateLambda([this](IConsoleVariable* Variable)
		{
			const bool bIsEnabled = Variable->GetBool();
			
			if (bIsEnabled)
			{
				SettingsManager->Initialize();
			}
			else
			{
				SettingsManager->Shutdown();
			}

			EnabledChangedDelegate.Broadcast();
		}));

	if (UE::Editor::Settings::Private::CVarTedsSettingsEnable.GetValueOnGameThread())
	{
		SettingsManager->Initialize();
	}
}

void UTedsSettingsEditorSubsystem::Deinitialize()
{
	UE_LOG(LogTedsSettings, Log, TEXT("UTedsSettingsEditorSubsystem::Deinitialize"));

	if (UE::Editor::Settings::Private::CVarTedsSettingsEnable.GetValueOnGameThread())
	{
		SettingsManager->Shutdown();
	}

	Super::Deinitialize();
}
