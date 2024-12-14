// Copyright Epic Games, Inc. All Rights Reserved.

#include "MovieGraphUtils.h"

#include "AudioMixerDevice.h"
#include "AudioThread.h"
#include "Engine/Engine.h"

#if WITH_EDITOR
#include "Engine/RendererSettings.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/PlatformFileManager.h"
#include "ISettingsEditorModule.h"
#include "Misc/ConfigCacheIni.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#endif // WITH_EDITOR

#define LOCTEXT_NAMESPACE "MovieGraphUtils"

namespace UE::MovieGraph
{
	FString GetUniqueName(const TArray<FString>& InExistingNames, const FString& InBaseName)
	{
		int32 Postfix = 0;
		FString NewName = InBaseName;

		while (InExistingNames.Contains(NewName))
		{
			Postfix++;
			NewName = FString::Format(TEXT("{0} {1}"), {InBaseName, Postfix});
		}

		return NewName;
	}

	namespace Audio
	{
		FAudioDevice* GetAudioDeviceFromWorldContext(const UObject* InWorldContextObject)
		{
			const UWorld* ThisWorld = GEngine->GetWorldFromContextObject(InWorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
			if (!ThisWorld || !ThisWorld->bAllowAudioPlayback || (ThisWorld->GetNetMode() == NM_DedicatedServer))
			{
				return nullptr;
			}

			return ThisWorld->GetAudioDeviceRaw();
		}

		::Audio::FMixerDevice* GetAudioMixerDeviceFromWorldContext(const UObject* InWorldContextObject)
		{
			if (FAudioDevice* AudioDevice = GetAudioDeviceFromWorldContext(InWorldContextObject))
			{
				return static_cast<::Audio::FMixerDevice*>(AudioDevice);
			}
	
			return nullptr;
		}

		bool IsMoviePipelineAudioOutputSupported(const UObject* InWorldContextObject)
		{
			const ::Audio::FMixerDevice* MixerDevice = GetAudioMixerDeviceFromWorldContext(InWorldContextObject);
			const ::Audio::IAudioMixerPlatformInterface* AudioMixerPlatform = MixerDevice ? MixerDevice->GetAudioMixerPlatform() : nullptr;

			// If the current audio mixer is non-realtime, audio output is supported
			if (AudioMixerPlatform && AudioMixerPlatform->IsNonRealtime())
			{
				return true;
			}

			// If there is no async audio processing (e.g. we're in the editor), it's possible to create a new non-realtime audio mixer
			if (!FAudioThread::IsUsingThreadedAudio())
			{
				return true;
			}

			// Otherwise, we can't support audio output
			return false;
		}
	}

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

	void ValidateAlphaProjectSettings(const FText& InRequestingFeatureName, bool bMandatePrimitiveAlphaHoldout)
	{
		URendererSettings* RendererSettings = GetMutableDefault<URendererSettings>();
		check(RendererSettings);

		const bool bAlphaOutputMissing = !RendererSettings->bEnableAlphaChannelInPostProcessing;
		const bool bPrimitiveHoldoutMissing = bMandatePrimitiveAlphaHoldout ? !RendererSettings->bDeferredSupportPrimitiveAlphaHoldout : false;

		if (bAlphaOutputMissing || bPrimitiveHoldoutMissing)
		{
			static TWeakPtr<SNotificationItem> NotificationItem;

			const FText AlphaOutputSettingOption = LOCTEXT("MovieAlphaSetting_AlphaOutput", "\n- Alpha Output");
			const FText PrimitiveHoldoutSettingOption = LOCTEXT("MovieAlphaSetting_PrimitiveHoldout", "\n- Support Primitive Alpha Holdout");
			const FText MovieAlphaText = FText::Format(LOCTEXT("MovieAlphaSettingPrompt", "The following project setting(s) must be enabled for Movie Render Graph's {0}:{1}{2}"), InRequestingFeatureName, bAlphaOutputMissing ? AlphaOutputSettingOption : FText::GetEmpty(), bPrimitiveHoldoutMissing ? PrimitiveHoldoutSettingOption : FText::GetEmpty());
			const FText MovieAlphaConfirmText = LOCTEXT("MovieAlphaSettingConfirm", "Enable");
			const FText MovieAlphaCancelText = LOCTEXT("MovieAlphaSettingCancel", "Not Now");

			/** Utility functions for notifications */
			struct FSuppressDialogOptions
			{
				static bool ShouldSuppressModal()
				{
					bool bSuppressNotification = false;
					GConfig->GetBool(TEXT("MovieRenderPipeline"), TEXT("SuppressMovieRenderPipelineAlphaPromptNotification"), bSuppressNotification, GEditorPerProjectIni);
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
					GConfig->SetBool(TEXT("MovieRenderPipeline"), TEXT("SuppressMovieRenderPipelineAlphaPromptNotification"), bSuppressNotification, GEditorPerProjectIni);
				}
			};

			// If the user has specified to supress this pop up, then just early out and exit	
			if (FSuppressDialogOptions::ShouldSuppressModal())
			{
				return;
			}

			FSimpleDelegate OnConfirmDelegate = FSimpleDelegate::CreateLambda(
				[RendererSettings, bAlphaOutputMissing, bPrimitiveHoldoutMissing]()
				{
					if (IsValid(RendererSettings))
					{
						if (bAlphaOutputMissing)
						{
							FProperty* Property = RendererSettings->GetClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(URendererSettings, bEnableAlphaChannelInPostProcessing));
							RendererSettings->PreEditChange(Property);

							RendererSettings->bEnableAlphaChannelInPostProcessing = true;

							FPropertyChangedEvent PropertyChangedEvent(Property, EPropertyChangeType::ValueSet, { RendererSettings });
							RendererSettings->PostEditChangeProperty(PropertyChangedEvent);
							UpdateDependentPropertyInConfigFile(RendererSettings, Property);
						}

						if (bPrimitiveHoldoutMissing)
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

						if (TSharedPtr<SNotificationItem> Item = NotificationItem.Pin())
						{
							Item->SetCompletionState(SNotificationItem::CS_Success);
							Item->ExpireAndFadeout();
						}

						NotificationItem.Reset();
					}
				}
			);

			FSimpleDelegate OnCancelDelegate = FSimpleDelegate::CreateLambda([]
				{
					if (TSharedPtr<SNotificationItem> Item = NotificationItem.Pin())
					{
						Item->SetCompletionState(SNotificationItem::CS_None);
						Item->ExpireAndFadeout();
					}

					NotificationItem.Reset();
				}
			);

			FNotificationInfo Info(MovieAlphaText);
			Info.bFireAndForget = false;
			Info.bUseLargeFont = false;
			Info.bUseThrobber = false;
			Info.bUseSuccessFailIcons = false;
			Info.ButtonDetails.Add(FNotificationButtonInfo(MovieAlphaConfirmText, FText(), OnConfirmDelegate));
			Info.ButtonDetails.Add(FNotificationButtonInfo(MovieAlphaCancelText, FText(), OnCancelDelegate));

			// Add a "Don't show this again" option
			Info.CheckBoxState = TAttribute<ECheckBoxState>::Create(&FSuppressDialogOptions::GetDontAskAgainCheckBoxState);
			Info.CheckBoxStateChanged = FOnCheckStateChanged::CreateStatic(&FSuppressDialogOptions::OnDontAskAgainCheckBoxStateChanged);
			Info.CheckBoxText = LOCTEXT("DefaultCheckBoxMessage", "Don't show this again");

			if (TSharedPtr<SNotificationItem> Item = NotificationItem.Pin())
			{
				Item->ExpireAndFadeout();
			}

			NotificationItem = FSlateNotificationManager::Get().AddNotification(Info);

			if (TSharedPtr<SNotificationItem> Item = NotificationItem.Pin())
			{
				Item->SetCompletionState(SNotificationItem::CS_Pending);
			}
		}
	}
#endif
}

#undef LOCTEXT_NAMESPACE // "MovieGraphUtils"
