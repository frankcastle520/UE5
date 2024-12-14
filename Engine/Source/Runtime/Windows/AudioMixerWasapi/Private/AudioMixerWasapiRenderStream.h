// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AudioMixer.h"	
#include "Misc/ScopeRWLock.h"
#include "WasapiAudioFormat.h"

#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/AllowWindowsPlatformAtomics.h"

#include "Microsoft/COMPointer.h"

THIRD_PARTY_INCLUDES_START
#include <mmdeviceapi.h>			// IMMNotificationClient
#include <audiopolicy.h>			// IAudioSessionEvents
THIRD_PARTY_INCLUDES_END

#include "Windows/HideWindowsPlatformAtomics.h"
#include "Windows/HideWindowsPlatformTypes.h"

DECLARE_DELEGATE(FAudioMixerReadNextBufferDelegate);

/**
 *  Warning: AudioMixerWasapi module is currently considered experimental and may change in the future.
 *           We do not recommend shipping projects with Experimental features.
 */

namespace Audio
{
	/** Defines parameters needed for opening a new audio stream to device. */
	struct FWasapiRenderStreamParams
	{
		/** The audio device to open. */
		TComPtr<IMMDevice> MMDevice;

		/** Hardware device configuration info. */
		FAudioPlatformDeviceInfo HardwareDeviceInfo;

		/** The number of desired audio frames in audio callback. */
		uint32 NumFrames = 0;

		/** The desired sample rate */
		uint32 SampleRate = 0;

		FWasapiRenderStreamParams() = default;

		FWasapiRenderStreamParams(const TComPtr<IMMDevice>& InMMDevice, 
			const FAudioPlatformDeviceInfo& InDeviceInfo, 
			const uint32 InNumFrames, 
			const uint32 InSampleRate) :
			MMDevice(InMMDevice),
			HardwareDeviceInfo(InDeviceInfo),
			NumFrames(InNumFrames),
			SampleRate(InSampleRate)
		{
		}
	};

	/**
	 * FAudioMixerWasapiRenderStream
	 */
	class FAudioMixerWasapiRenderStream
	{
	public:

		FAudioMixerWasapiRenderStream();
		virtual ~FAudioMixerWasapiRenderStream();

		bool InitializeHardware(const FWasapiRenderStreamParams& InParams);
		bool TeardownHardware();
		bool IsInitialized() const;
		int32 GetNumFrames(const int32 InNumReqestedFrames) const;
		bool OpenAudioStream(const FWasapiRenderStreamParams& InParams, HANDLE InEventHandle);
		bool CloseAudioStream();
		bool StartAudioStream();
		bool StopAudioStream();
		void SubmitBuffer(const uint8* Buffer, const SIZE_T InNumFrames);
		void DeviceRenderCallback();

		FAudioMixerReadNextBufferDelegate& OnReadNextBuffer() { return ReadNextBufferDelegate; }

	private:

		/** Delegate called each buffer callback to signal the mixer to process the next buffer. */
		FAudioMixerReadNextBufferDelegate ReadNextBufferDelegate;

		/** COM pointer to the WASAPI audio client object. */
		TComPtr<IAudioClient3> AudioClient;

		/** COM pointer to the WASAPI render client object. */
		TComPtr<IAudioRenderClient> RenderClient;

		/** Holds the audio format configuration for this stream. */
		FWasapiAudioFormat AudioFormat;

		/** Indicates if this object has been successfully initialized. */
		bool bIsInitialized = false;

		/** The state of the output audio stream. */
		EAudioOutputStreamState::Type StreamState = EAudioOutputStreamState::Closed;

		/** Render output device info. */
		FWasapiRenderStreamParams RenderStreamParams;

		/** The default callback period for this WASAPI render device. */
		uint32 DefaultDevicePeriod = 0;

		/** Number of frames of audio data which will be used for each audio callback. This value is 
		    determined by the WASAPI audio client and can be equal or greater than the number of frames requested. */
		uint32 NumFramesPerDeviceBuffer = 0;

		/** Accumulates errors that occur in the audio callback. */
		uint32 CallbackBufferErrors = 0;

		/** Pointer to WASAPI render audio buffer filled in each callback by the mixer in SubmitBuffer(). */
		uint8* RenderBufferPtr = nullptr;
	};
}
