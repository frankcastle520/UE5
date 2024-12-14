// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AudioMixer.h"	
#include "AudioMixerWasapiDeviceThread.h"
#include "AudioMixerWasapiRenderStream.h"
#include "Misc/ScopeRWLock.h"

#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/AllowWindowsPlatformAtomics.h"

#include "Microsoft/COMPointer.h"

THIRD_PARTY_INCLUDES_START
#include <mmdeviceapi.h>			// IMMNotificationClient
#include <audiopolicy.h>			// IAudioSessionEvents
THIRD_PARTY_INCLUDES_END

#include "Windows/HideWindowsPlatformAtomics.h"
#include "Windows/HideWindowsPlatformTypes.h"

/**
 *  Warning: AudioMixerWasapi module is currently considered experimental and may change in the future.
 *           We do not recommend shipping projects with Experimental features.
 */

namespace Audio
{
	/**
	 * FAudioMixerWasapi - Wasapi audio backend for Windows and Xbox
	 */
	class FAudioMixerWasapi : public IAudioMixerPlatformInterface
	{
	public:

		FAudioMixerWasapi();
		virtual ~FAudioMixerWasapi();

		//~ Begin IAudioMixerPlatformInterface
		virtual FString GetPlatformApi() const override { return TEXT("WASAPIMixer"); }
		virtual bool InitializeHardware() override;
		virtual bool TeardownHardware() override;
		virtual bool IsInitialized() const override;
		virtual int32 GetNumFrames(const int32 InNumReqestedFrames) override;
		virtual bool GetNumOutputDevices(uint32& OutNumOutputDevices) override;
		virtual bool GetOutputDeviceInfo(const uint32 InDeviceIndex, FAudioPlatformDeviceInfo& OutInfo) override;
		virtual FString GetCurrentDeviceName() const override;
		virtual bool GetDefaultOutputDeviceIndex(uint32& OutDefaultDeviceIndex) const override;
		virtual bool OpenAudioStream(const FAudioMixerOpenStreamParams& Params) override;
		virtual bool CloseAudioStream() override;
		virtual bool StartAudioStream() override;
		virtual bool StopAudioStream() override;
		virtual FAudioPlatformDeviceInfo GetPlatformDeviceInfo() const override;
		virtual void SubmitBuffer(const uint8* Buffer) override;
		virtual bool DisablePCMAudioCaching() const override { return true; }
		virtual FString GetDefaultDeviceName() override;
		virtual FAudioPlatformSettings GetPlatformSettings() const override;
		virtual IAudioPlatformDeviceInfoCache* GetDeviceInfoCache() const override;
		//~ End IAudioMixerPlatformInterface

		//~ Begin IAudioMixerDeviceChangedListener
		virtual void RegisterDeviceChangedListener() override;
		virtual void UnregisterDeviceChangedListener() override;
		virtual FString GetDeviceId() const override;
		//~ End IAudioMixerDeviceChangedListener

	private:

		/** Cache for holding information about MM audio devices (IMMDevice).  */
		TUniquePtr<IAudioPlatformDeviceInfoCache> DeviceInfoCache;

		/** Mutex for protecting shared resources during a device swap. */
		FCriticalSection AudioDeviceSwapCriticalSection;

		/** Used for determining if a device swap has been requested. */
		FString OriginalAudioDeviceId;

		/** The main audio device for outputting up to 8 channels. */
		TUniquePtr<FAudioMixerWasapiRenderStream> MainRenderStreamDevice;

		/** Indicates if this object has been successfully initialized. */
		bool bIsInitialized = false;

		/** The thread which provides an execution context during audio playback. */
		TUniquePtr<FAudioMixerWasapiDeviceThread> RenderDeviceThread;

		/** Fetches an IMMDevice with the given ID. */
		TComPtr<IMMDevice> GetMMDevice(const FString& InDeviceID) const;

		/** Initializes a Wasapi stream paramters struct with the give values. */
		bool InitStreamParams(const uint32 InDeviceIndex, const int32 InNumBufferFrames, const int32 InSampleRate, FWasapiRenderStreamParams& OutParams);

		/** Initializes a device with the given parameters. */
		bool InitAudioStreamDevice(const FWasapiRenderStreamParams& InStreamParams);
	};
 }
