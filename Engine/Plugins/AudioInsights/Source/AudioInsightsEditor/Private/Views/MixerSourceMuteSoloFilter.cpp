// Copyright Epic Games, Inc. All Rights Reserved.
#include "MixerSourceMuteSoloFilter.h"

#include "Audio/AudioDebug.h"
#include "AudioDeviceManager.h"
#include "Views/MixerSourceDashboardViewFactory.h"

namespace UE::Audio::Insights
{
	FMuteSoloFilter::FMuteSoloFilter()
	{
		FMixerSourceDashboardViewFactory::OnUpdateMuteSoloState.AddRaw(this, &FMuteSoloFilter::FilterMuteSolo);
	}

	FMuteSoloFilter::~FMuteSoloFilter()
	{
		FMixerSourceDashboardViewFactory::OnUpdateMuteSoloState.RemoveAll(this);
	}

	void FMuteSoloFilter::FilterMuteSolo(ECheckBoxState InMuteState, ECheckBoxState InSoloState, const FString& InCurrentFilterString) const
	{
#if ENABLE_AUDIO_DEBUG
		if (FAudioDeviceManager* AudioDeviceManager = FAudioDeviceManager::Get())
		{
			::Audio::FAudioDebugger& AudioDebugger = AudioDeviceManager->GetDebugger();

			FName CurrentFilterStringName{ InCurrentFilterString };
			
			if (InMuteState == ECheckBoxState::Checked && !InCurrentFilterString.IsEmpty())
			{
				AudioDebugger.ToggleMuteSoundWave(CurrentFilterStringName, true);
			}
			else
			{
				AudioDebugger.ToggleMuteSoundWave(NAME_None, true);
			}

			if (InSoloState == ECheckBoxState::Checked && !InCurrentFilterString.IsEmpty())
			{
				AudioDebugger.ToggleSoloSoundWave(CurrentFilterStringName, true);
			}
			else
			{
				AudioDebugger.ToggleSoloSoundWave(NAME_None, true);
			}
		}
#endif // ENABLE_AUDIO_DEBUG
	}
} // namespace UE::Audio::Insights
