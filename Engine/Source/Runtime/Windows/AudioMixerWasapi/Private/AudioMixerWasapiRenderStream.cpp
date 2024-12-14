// Copyright Epic Games, Inc. All Rights Reserved.

#include "AudioMixerWasapiRenderStream.h"

#include "AudioMixerWasapiLog.h"
#include "WasapiAudioUtils.h"

namespace Audio
{
	FAudioMixerWasapiRenderStream::FAudioMixerWasapiRenderStream()
	{
	}

	FAudioMixerWasapiRenderStream::~FAudioMixerWasapiRenderStream()
	{
	}

	bool FAudioMixerWasapiRenderStream::InitializeHardware(const FWasapiRenderStreamParams& InParams)
	{
		TComPtr<IAudioClient3> TempAudioClient;
		HRESULT Result = InParams.MMDevice->Activate(__uuidof(IAudioClient3), CLSCTX_INPROC_SERVER, nullptr, IID_PPV_ARGS_Helper(&TempAudioClient));
		if (FAILED(Result))
		{
			UE_LOG(LogAudioMixerWasapi, Error, TEXT("OpenAudioStream failed IMMDevice::Activate 0x%x"), (uint32)Result);
			return false;
		}

		AudioClientProperties AudioProps = { 0 };
		AudioProps.cbSize = sizeof(AudioClientProperties);
		AudioProps.bIsOffload = false;
		AudioProps.eCategory = AudioCategory_Media;

		Result = TempAudioClient->SetClientProperties(&AudioProps);
		if (FAILED(Result))
		{
			UE_LOG(LogAudioMixerWasapi, Error, TEXT("OpenAudioStream failed IAudioClient3::SetClientProperties 0x%x"), (uint32)Result);
			return false;
		}

		WAVEFORMATEX* MixFormat = nullptr;
		Result = TempAudioClient->GetMixFormat(&MixFormat);
		if (FAILED(Result) || !MixFormat)
		{
			UE_LOG(LogAudioMixerWasapi, Error, TEXT("OpenAudioStream failed IAudioClient3::GetMixFormat MixFormat: 0x%llx Result: 0x%x"), (uint64)MixFormat, (uint32)Result);
			return false;
		}

		FWasapiAudioFormat StreamFormat(FMath::Min<int32>(MixFormat->nChannels, AUDIO_MIXER_MAX_OUTPUT_CHANNELS), InParams.SampleRate, EWasapiAudioEncoding::FLOATING_POINT_32);

		if (MixFormat)
		{
			::CoTaskMemFree(MixFormat);
			MixFormat = nullptr;
		}

		REFERENCE_TIME DevicePeriodRefTime = 0;
		// The second param to GetDevicePeriod is only valid for exclusive mode
		// Note that GetDevicePeriod returns ref time which is sample rate agnostic
		Result = TempAudioClient->GetDevicePeriod(&DevicePeriodRefTime, nullptr);
		if (FAILED(Result))
		{
			UE_LOG(LogAudioMixerWasapi, Error, TEXT("OpenAudioStream failed IAudioClient3::GetDevicePeriod 0x%x"), (uint32)Result);
			return false;
		}

		DefaultDevicePeriod = FWasapiAudioUtils::RefTimeToFrames(DevicePeriodRefTime, InParams.SampleRate);

		REFERENCE_TIME DesiredBufferDuration = FWasapiAudioUtils::FramesToRefTime(FMath::Max<uint32>(InParams.NumFrames, DefaultDevicePeriod), InParams.SampleRate);
		// Add two times the device period to the buffer size to ensure buffer phasing isn't an issue with larger buffer sizes.
		// For example, with an engine period of 1024 and a device period of 480, we'll have two device periods to fetch the next engine buffer before underruning. 
		DesiredBufferDuration += (DevicePeriodRefTime * 2);

		// For shared mode, this is required to be zero
		const REFERENCE_TIME Periodicity = 0;

		// Audio events will be delivered to us rather than needing to poll
		uint32 Flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;

		if (InParams.SampleRate != InParams.HardwareDeviceInfo.SampleRate)
		{
			Flags |= AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM;
			UE_LOG(LogAudioMixerWasapi, Warning, TEXT("Sample rate mismatch. Engine sample rate: %d Device sample rate: %d"), InParams.SampleRate, InParams.HardwareDeviceInfo.SampleRate);
			UE_LOG(LogAudioMixerWasapi, Warning, TEXT("Device level sample rate conversion will be used."));
		}

		Result = TempAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, Flags, DesiredBufferDuration, Periodicity, StreamFormat.GetWaveFormat(), nullptr);
		if (FAILED(Result))
		{
			UE_LOG(LogAudioMixerWasapi, Error, TEXT("OpenAudioStream failed IAudioClient3::Initialize 0x%x"), (uint32)Result);
			return false;
		}

		Result = TempAudioClient->GetBufferSize(&NumFramesPerDeviceBuffer);
		if (FAILED(Result))
		{
			UE_LOG(LogAudioMixerWasapi, Error, TEXT("OpenAudioStream failed IAudioClient3::GetBufferSize 0x%x"), (uint32)Result);
			return false;
		}

		AudioClient = MoveTemp(TempAudioClient);
		AudioFormat = StreamFormat;
		RenderStreamParams = InParams;

		bIsInitialized = true;

		UE_LOG(LogAudioMixerWasapi, Verbose, TEXT("FAudioMixerWasapiRenderStream::InitializeHardware succeeded with sample rate: %d, buffer period: %d"), InParams.SampleRate, InParams.NumFrames);

		return true;
	}

	bool FAudioMixerWasapiRenderStream::TeardownHardware()
	{
		if (!bIsInitialized)
		{
			UE_LOG(LogAudioMixerWasapi, Warning, TEXT("FAudioMixerWasapiRenderStream::TeardownHardware failed...not initialized. "));
			return false;
		}

		ReadNextBufferDelegate.Unbind();
		RenderClient.Reset();
		AudioClient.Reset();

		bIsInitialized = false;

		UE_LOG(LogAudioMixerWasapi, Verbose, TEXT("FAudioMixerWasapiRenderStream::TeardownHardware succeeded"));

		return true;
	}

	bool FAudioMixerWasapiRenderStream::IsInitialized() const
	{
		return bIsInitialized;
	}

	int32 FAudioMixerWasapiRenderStream::GetNumFrames(const int32 InNumReqestedFrames) const
	{
		return FMath::Max<uint32>(InNumReqestedFrames, DefaultDevicePeriod);
	}

	bool FAudioMixerWasapiRenderStream::OpenAudioStream(const FWasapiRenderStreamParams& InParams, HANDLE InEventHandle)
	{
		if (InParams.HardwareDeviceInfo.DeviceId != RenderStreamParams.HardwareDeviceInfo.DeviceId)
		{
			if (!InitializeHardware(InParams))
			{
				UE_LOG(LogAudioMixerWasapi, Error, TEXT("OpenAudioStream failed InitAudioClient"));
				return false;
			}
		}

		if (InEventHandle == nullptr)
		{
			UE_LOG(LogAudioMixerWasapi, Error, TEXT("OpenAudioStream null EventHandle"));
			return false;
		}

		HRESULT Result = AudioClient->SetEventHandle(InEventHandle);
		if (FAILED(Result))
		{
			UE_LOG(LogAudioMixerWasapi, Error, TEXT("OpenAudioStream failed IAudioClient3::SetEventHandle 0x%x"), (uint32)Result);
			return false;
		}

		TComPtr<IAudioRenderClient> TempRenderClient;
		Result = AudioClient->GetService(__uuidof(IAudioRenderClient), IID_PPV_ARGS_Helper(&TempRenderClient));
		if (FAILED(Result))
		{
			UE_LOG(LogAudioMixerWasapi, Error, TEXT("OpenAudioStream failed IAudioClient3::GetService IAudioRenderClient 0x%x"), (uint32)Result);
			return false;
		}

		RenderClient = MoveTemp(TempRenderClient);
		bIsInitialized = true;

		UE_LOG(LogAudioMixerWasapi, Verbose, TEXT("FAudioMixerWasapiRenderStream::OpenAudioStream succeeded with SampeRate: %d, NumFrames: %d"), InParams.SampleRate, InParams.NumFrames);

		return true;
	}

	bool FAudioMixerWasapiRenderStream::CloseAudioStream()
	{
		if (!bIsInitialized || StreamState == EAudioOutputStreamState::Closed)
		{
			UE_LOG(LogAudioMixerWasapi, Verbose, TEXT("FAudioMixerWasapiRenderStream::CloseAudioStream stream appears to be already closed"));

			return false;
		}

		StreamState = EAudioOutputStreamState::Closed;

		return true;
	}

	bool FAudioMixerWasapiRenderStream::StartAudioStream()
	{
		if (bIsInitialized)
		{
			StreamState = EAudioOutputStreamState::Running;

			if (!AudioClient.IsValid())
			{
				UE_LOG(LogAudioMixerWasapi, Error, TEXT("StartAudioStream failed invalid audio client"));
				return false;
			}

			if (!ReadNextBufferDelegate.IsBound())
			{
				UE_LOG(LogAudioMixerWasapi, Error, TEXT("StartAudioStream failed buffer delegate not bound"));
				return false;
			}

			AudioClient->Start();
		}

		UE_LOG(LogAudioMixerWasapi, Verbose, TEXT("FAudioMixerWasapiRenderStream::StartAudioStream stream started"));

		return true;
	}

	bool FAudioMixerWasapiRenderStream::StopAudioStream()
	{
		if (!bIsInitialized)
		{
			UE_LOG(LogAudioMixerWasapi, Error, TEXT("FAudioMixerWasapiRenderStream::StopAudioStream() not initialized"));
			return false;
		}

		if (StreamState != EAudioOutputStreamState::Stopped && StreamState != EAudioOutputStreamState::Closed)
		{
			if (AudioClient.IsValid())
			{
				AudioClient->Stop();
			}

			StreamState = EAudioOutputStreamState::Stopped;
		}

		if (CallbackBufferErrors > 0)
		{
			UE_LOG(LogAudioMixerWasapi, Error, TEXT("FAudioMixerWasapiRenderStream::StopAudioStream render stream reported %d callback buffer errors"), CallbackBufferErrors);
			CallbackBufferErrors = 0;
		}

		return true;
	}

	void FAudioMixerWasapiRenderStream::DeviceRenderCallback()
	{
		SCOPED_NAMED_EVENT(FAudioMixerWasapiRenderStream_DeviceRenderCallback, FColor::Blue);

		if (bIsInitialized)
		{
			uint32 NumFramesPadding = 0;
			AudioClient->GetCurrentPadding(&NumFramesPadding);

			// NumFramesPerDeviceBuffer is the buffer size WASAPI allocated. It is gauranteed to 
			// be at least the amount requested. For example, if we request a 1024 frame buffer, WASAPI
			// might allocate a 1056 frame buffer. The padding is subtracted from the allocated amount
			// to determine how much space is available currently in the buffer.
			const int32 NumFramesAvailable = NumFramesPerDeviceBuffer - NumFramesPadding;
			if (NumFramesAvailable >= static_cast<int32>(RenderStreamParams.NumFrames))
			{
				check(!RenderBufferPtr);

				if (SUCCEEDED(RenderClient->GetBuffer(RenderStreamParams.NumFrames, &RenderBufferPtr)))
				{
					ReadNextBufferDelegate.Execute();

					HRESULT Result = RenderClient->ReleaseBuffer(RenderStreamParams.NumFrames, 0 /* flags */);
					if (FAILED(Result))
					{
						++CallbackBufferErrors;
					}

					RenderBufferPtr = nullptr;
				}
				else
				{
					++CallbackBufferErrors;
				}
			}
		}
	}

	void FAudioMixerWasapiRenderStream::SubmitBuffer(const uint8* InBuffer, const SIZE_T InNumFrames)
	{
		if (RenderBufferPtr)
		{
			FMemory::Memcpy(RenderBufferPtr, InBuffer, InNumFrames * AudioFormat.GetFrameSizeInBytes());
		}
	}
}
