// Copyright Epic Games, Inc. All Rights Reserved.

#include "AudioMixerWasapi.h"
#include "WasapiAudioUtils.h"

namespace Audio
{
	FAudioMixerWasapi::FAudioMixerWasapi()
	{
	}

	FAudioMixerWasapi::~FAudioMixerWasapi()
	{
	}

	bool FAudioMixerWasapi::InitializeHardware()
	{
		SCOPED_NAMED_EVENT(FAudioMixerWasapi_InitializeHardware, FColor::Blue);

		RegisterDeviceChangedListener();

		FAudioPlatformSettings EngineSettings = GetPlatformSettings();

		MainRenderStreamDevice = MakeUnique<FAudioMixerWasapiRenderStream>();
		if (!MainRenderStreamDevice.IsValid())
		{
			UE_LOG(LogAudioMixer, Error, TEXT("FAudioMixerWasapi::InitializeHardware failed to create MainRenderStreamDevice"));
			return false;
		}

		MainRenderStreamDevice->OnReadNextBuffer().BindRaw(this, &FAudioMixerWasapi::ReadNextBuffer);

		FWasapiRenderStreamParams StreamParams;
		if (!InitStreamParams(AUDIO_MIXER_DEFAULT_DEVICE_INDEX, EngineSettings.CallbackBufferFrameSize, EngineSettings.SampleRate, StreamParams))
		{
			return false;
		}

		if (!InitAudioStreamDevice(StreamParams))
		{
			UE_LOG(LogAudioMixer, Error, TEXT("FAudioMixerWasapi::InitializeHardware failed InitAudioStreamDevice"));
			return false;
		}

		return true;
	}

	bool FAudioMixerWasapi::TeardownHardware()
	{
		if (!bIsInitialized)
		{
			AUDIO_PLATFORM_LOG_ONCE(TEXT("FAudioMixerWasapi::TeardownHardware failed...not initialized."), Warning);
			return false;
		}

		if (MainRenderStreamDevice.IsValid())
		{
			// Teardown the main device which will also unbind our delegate
			MainRenderStreamDevice->TeardownHardware();
			MainRenderStreamDevice.Reset();
		}

		bIsInitialized = false;

		return true;
	}

	bool FAudioMixerWasapi::IsInitialized() const
	{
		return bIsInitialized && MainRenderStreamDevice.IsValid() && MainRenderStreamDevice->IsInitialized();
	}

	int32 FAudioMixerWasapi::GetNumFrames(const int32 InNumReqestedFrames)
	{
		if (MainRenderStreamDevice.IsValid())
		{
			return MainRenderStreamDevice->GetNumFrames(InNumReqestedFrames);
		}

		return InNumReqestedFrames;
	}

	bool FAudioMixerWasapi::GetNumOutputDevices(uint32& OutNumOutputDevices)
	{
		SCOPED_NAMED_EVENT(FAudioMixerWasapi_GetNumOutputDevices, FColor::Blue);

		OutNumOutputDevices = 0;

		if (IAudioPlatformDeviceInfoCache* Cache = GetDeviceInfoCache())
		{
			OutNumOutputDevices = Cache->GetAllActiveOutputDevices().Num();
			return true;
		}
		else
		{
			AUDIO_PLATFORM_LOG_ONCE(TEXT("FAudioMixerWasapi device cache not initialized"), Warning);
			return false;
		}
	}

	bool FAudioMixerWasapi::GetOutputDeviceInfo(const uint32 InDeviceIndex, FAudioPlatformDeviceInfo& OutInfo)
	{
		SCOPED_NAMED_EVENT(FAudioMixerWasapi_GetOutputDeviceInfo, FColor::Blue);

		if (IAudioPlatformDeviceInfoCache* Cache = GetDeviceInfoCache())
		{
			if (InDeviceIndex == AUDIO_MIXER_DEFAULT_DEVICE_INDEX)
			{
				if (TOptional<FAudioPlatformDeviceInfo> Defaults = Cache->FindDefaultOutputDevice())
				{
					OutInfo = *Defaults;
					return true;
				}
			}
			else
			{
				TArray<FAudioPlatformDeviceInfo> ActiveDevices = Cache->GetAllActiveOutputDevices();
				if (ActiveDevices.IsValidIndex(InDeviceIndex))
				{
					OutInfo = ActiveDevices[InDeviceIndex];
					return true;
				}
			}
		}

		return false;
	}

	FString FAudioMixerWasapi::GetCurrentDeviceName() const
	{
		return AudioStreamInfo.DeviceInfo.Name;
	}

	bool FAudioMixerWasapi::GetDefaultOutputDeviceIndex(uint32& OutDefaultDeviceIndex) const
	{
		OutDefaultDeviceIndex = AUDIO_MIXER_DEFAULT_DEVICE_INDEX;
		return true;
	}

	bool FAudioMixerWasapi::InitStreamParams(const uint32 InDeviceIndex, const int32 InNumBufferFrames, const int32 InSampleRate, FWasapiRenderStreamParams& OutParams)
	{
		FAudioPlatformDeviceInfo DeviceInfo;
		if (!GetOutputDeviceInfo(InDeviceIndex, DeviceInfo))
		{
			UE_LOG(LogAudioMixer, Error, TEXT("FAudioMixerWasapi::InitStreamParams unable to find default device"));
			return false;
		}

		TComPtr<IMMDevice> MMDevice = GetMMDevice(DeviceInfo.DeviceId);
		if (!MMDevice)
		{
			UE_LOG(LogAudioMixer, Error, TEXT("FAudioMixerWasapi::InitStreamParams null MMDevice"));
			return false;
		}

		OutParams = FWasapiRenderStreamParams(MMDevice, DeviceInfo, InNumBufferFrames, InSampleRate);

		return true;
	}

	bool FAudioMixerWasapi::InitAudioStreamDevice(const FWasapiRenderStreamParams& InStreamParams)
	{
		if (ensure(MainRenderStreamDevice.IsValid()))
		{
			return MainRenderStreamDevice->InitializeHardware(InStreamParams);
		}

		return false;
	}

	bool FAudioMixerWasapi::OpenAudioStream(const FAudioMixerOpenStreamParams& Params)
	{
		OpenStreamParams = Params;

		AudioStreamInfo.Reset();

		AudioStreamInfo.OutputDeviceIndex = OpenStreamParams.OutputDeviceIndex;
		AudioStreamInfo.NumOutputFrames = OpenStreamParams.NumFrames;
		AudioStreamInfo.NumBuffers = OpenStreamParams.NumBuffers;
		AudioStreamInfo.AudioMixer = OpenStreamParams.AudioMixer;

		FWasapiRenderStreamParams StreamParams;
		if (!InitStreamParams(OpenStreamParams.OutputDeviceIndex, OpenStreamParams.NumFrames, OpenStreamParams.SampleRate, StreamParams))
		{
			return false;
		}

		AudioStreamInfo.DeviceInfo = StreamParams.HardwareDeviceInfo;
		
		HANDLE EventHandle = nullptr;
		const TFunction<void()> RenderCallback = [this]() { MainRenderStreamDevice->DeviceRenderCallback(); };
		RenderDeviceThread = MakeUnique<FAudioMixerWasapiDeviceThread>(RenderCallback, EventHandle);
		
		if (!RenderDeviceThread.IsValid())
		{
			UE_LOG(LogAudioMixer, Error, TEXT("Unable to create RenderDeviceThread"));
			return false;
		}

		if (EventHandle == nullptr)
		{
			UE_LOG(LogAudioMixer, Error, TEXT("OpenAudioStream null EventHandle"));
			return false;
		}

		if (!MainRenderStreamDevice->OpenAudioStream(StreamParams, EventHandle))
		{
			UE_LOG(LogAudioMixer, Error, TEXT("OpenAudioStream failed to open main audio device"));
			return false;
		}

		bIsInitialized = true;

		UE_LOG(LogAudioMixer, Display, TEXT("FAudioMixerWasapi initialized SampeRate: %d"), OpenStreamParams.SampleRate);

		return true;
	}

	bool FAudioMixerWasapi::CloseAudioStream()
	{
		if (!bIsInitialized || AudioStreamInfo.StreamState == EAudioOutputStreamState::Closed)
		{
			return false;
		}

		MainRenderStreamDevice->CloseAudioStream();

		AudioStreamInfo.StreamState = EAudioOutputStreamState::Closed;

		return true;
	}

	bool FAudioMixerWasapi::StartAudioStream()
	{
		if (IsInitialized() && RenderDeviceThread.IsValid())
		{
			BeginGeneratingAudio();

			AudioStreamInfo.StreamState = EAudioOutputStreamState::Running;

			MainRenderStreamDevice->StartAudioStream();
			RenderDeviceThread->Start();

			return true;
		}

		return false;
	}

	bool FAudioMixerWasapi::StopAudioStream()
	{
		if (!bIsInitialized)
		{
			AUDIO_PLATFORM_LOG_ONCE(TEXT("FAudioMixerWasapi::StopAudioStream() not initialized."), Warning);
			return false;
		}

		if (AudioStreamInfo.StreamState != EAudioOutputStreamState::Stopped && AudioStreamInfo.StreamState != EAudioOutputStreamState::Closed)
		{
			if (AudioStreamInfo.StreamState == EAudioOutputStreamState::Running)
			{
				StopGeneratingAudio();
			}

			if (RenderDeviceThread.IsValid())
			{
				RenderDeviceThread->Stop();
			}

			if (MainRenderStreamDevice.IsValid())
			{
				MainRenderStreamDevice->StopAudioStream();
			}

			check(AudioStreamInfo.StreamState == EAudioOutputStreamState::Stopped);
		}

		return true;
	}

	FAudioPlatformDeviceInfo FAudioMixerWasapi::GetPlatformDeviceInfo() const
	{
		return AudioStreamInfo.DeviceInfo;
	}

	void FAudioMixerWasapi::SubmitBuffer(const uint8* InBuffer)
	{
		if (MainRenderStreamDevice.IsValid())
		{
			MainRenderStreamDevice->SubmitBuffer(InBuffer, OpenStreamParams.NumFrames);
		}
	}

	FString FAudioMixerWasapi::GetDefaultDeviceName()
	{
		return FString();
	}

	FAudioPlatformSettings FAudioMixerWasapi::GetPlatformSettings() const
	{
#if WITH_ENGINE
		return FAudioPlatformSettings::GetPlatformSettings(FPlatformProperties::GetRuntimeSettingsClassName());
#else
		return FAudioPlatformSettings();
#endif // WITH_ENGINE
	}

	IAudioPlatformDeviceInfoCache* FAudioMixerWasapi::GetDeviceInfoCache() const
	{
		return DeviceInfoCache.Get();
	}
}
