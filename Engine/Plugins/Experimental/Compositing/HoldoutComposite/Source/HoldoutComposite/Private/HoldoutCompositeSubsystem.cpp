// Copyright Epic Games, Inc. All Rights Reserved.

#include "HoldoutCompositeSubsystem.h"
#include "HoldoutCompositeModule.h"
#include "HoldoutCompositeSceneViewExtension.h"

#include "Engine/RendererSettings.h"

#if WITH_EDITOR
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/PlatformFileManager.h"
#include "ISettingsEditorModule.h"
#include "Misc/ConfigCacheIni.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#endif

#define LOCTEXT_NAMESPACE "HoldoutCompositeSubsystem"

namespace
{
#if WITH_EDITOR
	void UpdateDependentPropertyInConfigFile(URendererSettings* RendererSettings, FProperty* RendererProperty)
	{
		FString RelativePath = RendererSettings->GetDefaultConfigFilename();
		FString FullPath = FPaths::ConvertRelativePathToFull(RelativePath);

		const bool bIsWriteable = !FPlatformFileManager::Get().GetPlatformFile().IsReadOnly(*FullPath);

		if (!bIsWriteable)
		{
			FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*FullPath, false);
		}

		RendererSettings->UpdateSinglePropertyInConfigFile(RendererProperty, RendererSettings->GetDefaultConfigFilename());

		// Restore original state for source control
		if (!bIsWriteable)
		{
			FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*FullPath, true);
		}
	}
#endif
}

UHoldoutCompositeSubsystem::UHoldoutCompositeSubsystem()
{
}

void UHoldoutCompositeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	UWorld* World = GetWorld();
	check(IsValid(World));

	HoldoutCompositeViewExtension = FSceneViewExtensions::NewExtension<FHoldoutCompositeSceneViewExtension>(World);
}

void UHoldoutCompositeSubsystem::Deinitialize()
{
	HoldoutCompositeViewExtension.Reset();
	
	Super::Deinitialize();
}

void UHoldoutCompositeSubsystem::RegisterPrimitive(TSoftObjectPtr<UPrimitiveComponent> InPrimitiveComponent, bool bInHoldoutState)
{
	TArray<TSoftObjectPtr<UPrimitiveComponent>> PrimitiveComponents = { InPrimitiveComponent };

	RegisterPrimitives(PrimitiveComponents, bInHoldoutState);
}

void UHoldoutCompositeSubsystem::RegisterPrimitives(TArrayView<TSoftObjectPtr<UPrimitiveComponent>> InPrimitiveComponents, bool bInHoldoutState)
{
	const bool bValidSettings = ValidateProjectSettings();

	if (bValidSettings && HoldoutCompositeViewExtension.IsValid())
	{
		HoldoutCompositeViewExtension->RegisterPrimitives(MoveTemp(InPrimitiveComponents), bInHoldoutState);
	}
}

void UHoldoutCompositeSubsystem::UnregisterPrimitive(TSoftObjectPtr<UPrimitiveComponent> InPrimitiveComponent, bool bInHoldoutState)
{
	TArray<TSoftObjectPtr<UPrimitiveComponent>> PrimitiveComponents = { InPrimitiveComponent };
	
	UnregisterPrimitives(PrimitiveComponents, bInHoldoutState);
}

void UHoldoutCompositeSubsystem::UnregisterPrimitives(TArrayView<TSoftObjectPtr<UPrimitiveComponent>> InPrimitiveComponents, bool bInHoldoutState)
{
	if (HoldoutCompositeViewExtension.IsValid())
	{
		HoldoutCompositeViewExtension->UnregisterPrimitives(MoveTemp(InPrimitiveComponents), bInHoldoutState);
	}
}

bool UHoldoutCompositeSubsystem::ValidateProjectSettings()
{
	URendererSettings* RendererSettings = GetMutableDefault<URendererSettings>();
	check(RendererSettings);

	const bool bValidSettings = RendererSettings->bEnableAlphaChannelInPostProcessing && RendererSettings->bDeferredSupportPrimitiveAlphaHoldout;

	if (!bValidSettings)
	{
#if WITH_EDITOR
		// We inform the user and offer them the option to activate project settings.
		UE_CALL_ONCE([&]{ PrimitiveHoldoutSettingsNotification(RendererSettings); });
#else
		UE_CALL_ONCE([&]{ UE_LOG(LogHoldoutComposite, Warning, TEXT("Both \"Alpha Output\" and \"Support Primitive Alpha Holdout\" project settings must be enabled for holdout composite.")); });
#endif
	}

	return bValidSettings;
}

#if WITH_EDITOR
void UHoldoutCompositeSubsystem::PrimitiveHoldoutSettingsNotification(URendererSettings* RendererSettings)
{
	const bool bAlphaOutputMissing = !RendererSettings->bEnableAlphaChannelInPostProcessing;
	const bool bPrimitiveHoldoutMissing = !RendererSettings->bDeferredSupportPrimitiveAlphaHoldout;

	const FText AlphaOutputSettingOption = LOCTEXT("HoldoutSetting_AlphaOutput", "\n- Alpha Output");
	const FText PrimitiveHoldoutSettingOption = LOCTEXT("HoldoutSetting_PrimitiveHoldout", "\n- Support Primitive Alpha Holdout");
	const FText HoldoutText = FText::Format(LOCTEXT("HoldoutSettingPrompt", "The following project setting(s) must be enabled for holdout composite:{0}{1}"), bAlphaOutputMissing ? AlphaOutputSettingOption : FText::GetEmpty(), bPrimitiveHoldoutMissing ? PrimitiveHoldoutSettingOption : FText::GetEmpty());
	const FText HoldoutConfirmText = LOCTEXT("HoldoutSettingConfirm", "Enable");
	const FText HoldoutCancelText = LOCTEXT("HoldoutSettingCancel", "Not Now");

	/** Utility functions for notifications */
	struct FSuppressDialogOptions
	{
		static bool ShouldSuppressModal()
		{
			bool bSuppressNotification = false;
			GConfig->GetBool(TEXT("HoldoutComposite"), TEXT("SuppressHoldoutCompositePromptNotification"), bSuppressNotification, GEditorPerProjectIni);
			return bSuppressNotification;
		}

		static ECheckBoxState GetDontAskAgainCheckBoxState()
		{
			return ShouldSuppressModal() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		}

		static void OnDontAskAgainCheckBoxStateChanged(ECheckBoxState NewState)
		{
			// If the user selects to not show this again, set that in the config so we know about it in between sessions
			const bool bSuppressNotification = (NewState == ECheckBoxState::Checked);
			GConfig->SetBool(TEXT("HoldoutComposite"), TEXT("SuppressHoldoutCompositePromptNotification"), bSuppressNotification, GEditorPerProjectIni);
		}
	};

	// If the user has specified to supress this pop up, then just early out and exit	
	if (FSuppressDialogOptions::ShouldSuppressModal())
	{
		return;
	}

	FSimpleDelegate OnConfirmDelegate = FSimpleDelegate::CreateLambda(
		[WeakThis = MakeWeakObjectPtr(this), RendererSettings]()
		{
			if (IsValid(RendererSettings))
			{
				if (!RendererSettings->bDeferredSupportPrimitiveAlphaHoldout)
				{
					FProperty* Property = RendererSettings->GetClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(URendererSettings, bDeferredSupportPrimitiveAlphaHoldout));
					RendererSettings->PreEditChange(Property);

					RendererSettings->bDeferredSupportPrimitiveAlphaHoldout = true;

					FPropertyChangedEvent PropertyChangedEvent(Property, EPropertyChangeType::ValueSet, { RendererSettings });
					RendererSettings->PostEditChangeProperty(PropertyChangedEvent);
					UpdateDependentPropertyInConfigFile(RendererSettings, Property);

					// SupportPrimitiveAlphaHoldout requires shader recompilation, ask for a restart.
					FModuleManager::GetModuleChecked<ISettingsEditorModule>("SettingsEditor").OnApplicationRestartRequired();
				}

				if (!RendererSettings->bEnableAlphaChannelInPostProcessing)
				{
					FProperty* Property = RendererSettings->GetClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(URendererSettings, bEnableAlphaChannelInPostProcessing));
					RendererSettings->PreEditChange(Property);

					RendererSettings->bEnableAlphaChannelInPostProcessing = true;

					FPropertyChangedEvent PropertyChangedEvent(Property, EPropertyChangeType::ValueSet, { RendererSettings });
					RendererSettings->PostEditChangeProperty(PropertyChangedEvent);
					UpdateDependentPropertyInConfigFile(RendererSettings, Property);
				}
			}

			TStrongObjectPtr<UHoldoutCompositeSubsystem> Subsystem = WeakThis.Pin();
			if (Subsystem.IsValid())
			{
				TSharedPtr<SNotificationItem> NotificationItem = Subsystem->HoldoutNotificationItem.Pin();
				if (NotificationItem.IsValid())
				{
					NotificationItem->SetCompletionState(SNotificationItem::CS_Success);
					NotificationItem->ExpireAndFadeout();
				}

				Subsystem->HoldoutNotificationItem.Reset();
			}
		}
	);

	FSimpleDelegate OnCancelDelegate = FSimpleDelegate::CreateLambda(
		[WeakThis = MakeWeakObjectPtr(this)]()
		{
			TStrongObjectPtr<UHoldoutCompositeSubsystem> Subsystem = WeakThis.Pin();
			if (Subsystem.IsValid())
			{
				TSharedPtr<SNotificationItem> NotificationItem = Subsystem->HoldoutNotificationItem.Pin();
				if (NotificationItem.IsValid())
				{
					NotificationItem->SetCompletionState(SNotificationItem::CS_None);
					NotificationItem->ExpireAndFadeout();
				}

				Subsystem->HoldoutNotificationItem.Reset();
			}
		}
	);

	FNotificationInfo Info(HoldoutText);
	Info.bFireAndForget = false;
	Info.bUseLargeFont = false;
	Info.bUseThrobber = false;
	Info.bUseSuccessFailIcons = false;
	Info.ButtonDetails.Add(FNotificationButtonInfo(HoldoutConfirmText, FText(), OnConfirmDelegate));
	Info.ButtonDetails.Add(FNotificationButtonInfo(HoldoutCancelText, FText(), OnCancelDelegate));

	// Add a "Don't show this again" option
	Info.CheckBoxState = TAttribute<ECheckBoxState>::Create(&FSuppressDialogOptions::GetDontAskAgainCheckBoxState);
	Info.CheckBoxStateChanged = FOnCheckStateChanged::CreateStatic(&FSuppressDialogOptions::OnDontAskAgainCheckBoxStateChanged);
	Info.CheckBoxText = LOCTEXT("DontShowThisAgainCheckBoxMessage", "Don't show this again");

	if (HoldoutNotificationItem.IsValid())
	{
		HoldoutNotificationItem.Pin()->ExpireAndFadeout();
		HoldoutNotificationItem.Reset();
	}

	HoldoutNotificationItem = FSlateNotificationManager::Get().AddNotification(Info);

	if (HoldoutNotificationItem.IsValid())
	{
		HoldoutNotificationItem.Pin()->SetCompletionState(SNotificationItem::CS_Pending);
	}
}
#endif

#undef LOCTEXT_NAMESPACE

