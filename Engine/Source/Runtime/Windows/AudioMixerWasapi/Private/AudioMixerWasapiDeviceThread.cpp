// Copyright Epic Games, Inc. All Rights Reserved.

#include "AudioMixerWasapiDeviceThread.h"
#include "AudioMixer.h"	
#include "AudioMixerWasapiLog.h"	


namespace Audio
{
	FAudioMixerWasapiRunnable::FAudioMixerWasapiRunnable(const TFunction<void()>& InDeviceRenderCallback, HANDLE& OutEventHandle) :
		DeviceRenderCallback(InDeviceRenderCallback)
	{
		// Not using FEvent/FEventWin here because we need access to the raw, platform
		// handle (see SetEventHandler() below).
		EventHandle = ::CreateEvent(nullptr, 0, 0, nullptr);
		if (EventHandle == nullptr)
		{
			UE_LOG(LogAudioMixerWasapi, Error, TEXT("FAudioMixerWasapiRunnable CreateEvent failed"));
		}

		OutEventHandle = EventHandle;
	}

	uint32 FAudioMixerWasapiRunnable::Run()
	{
		bIsRunning = true;

		bool bCoInitialized = FWindowsPlatformMisc::CoInitialize(ECOMModel::Multithreaded);

		while (bIsRunning.load())
		{
			static constexpr uint32 TimeoutInMs = 1000;

			int32 Result = ::WaitForSingleObject(EventHandle, TimeoutInMs);
			if (Result == WAIT_TIMEOUT)
			{
				++OutputStreamTimeoutsDetected;
			}
			else if (Result == WAIT_OBJECT_0)
			{
				DeviceRenderCallback();
			}
		}

		if (bCoInitialized)
		{
			FWindowsPlatformMisc::CoUninitialize();
		}

		return 0;
	}

	void FAudioMixerWasapiRunnable::Stop()
	{
		bIsRunning = false;
		if (OutputStreamTimeoutsDetected > 0)
		{
			UE_LOG(LogAudioMixerWasapi, Error, TEXT("FAudioMixerWasapiRunnable::Stop render stream reported %d timeouts"), OutputStreamTimeoutsDetected);
		}
	}

	FAudioMixerWasapiDeviceThread::FAudioMixerWasapiDeviceThread(const TFunction<void()>& InDeviceRenderCallback, HANDLE& OutEventHandle) :
		DeviceRenderRunnable(MakeUnique<FAudioMixerWasapiRunnable>(InDeviceRenderCallback, OutEventHandle))
	{
	}

	bool FAudioMixerWasapiDeviceThread::Start()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Audio::FAudioMixerWasapiDeviceThread::Start);
		check(DeviceRenderThread == nullptr);

		DeviceRenderThread = TUniquePtr<FRunnableThread>(FRunnableThread::Create(DeviceRenderRunnable.Get(), TEXT("Audio Render Device Thread"), 0, TPri_TimeCritical));
		return DeviceRenderThread.IsValid();
	}

	void FAudioMixerWasapiDeviceThread::Stop()
	{
		if (DeviceRenderThread.IsValid())
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(Audio::FAudioMixerWasapiDeviceThread::Stop);

			bool bShouldWait = true;
			DeviceRenderThread->Kill(bShouldWait);
		}
	}

	void FAudioMixerWasapiDeviceThread::Abort()
	{
		if (DeviceRenderThread.IsValid())
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(Audio::FAudioMixerWasapiDeviceThread::Abort);

			// Always wait for thread to complete otherwise we can crash if
			// the stream is disposed of mid-callback.
			bool bShouldWait = true;
			DeviceRenderThread->Kill(bShouldWait);
		}
	}
}
