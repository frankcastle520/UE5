// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <atomic>

#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Templates/Function.h"
#include "Templates/UniquePtr.h"

#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/AllowWindowsPlatformAtomics.h"

#include "Microsoft/COMPointer.h"

THIRD_PARTY_INCLUDES_START
#include <winnt.h>
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
	 * FAudioMixerWasapiRunnable - The runnable which executes the main thread loop for FWasapiCaptureThread.
	 */
	class FAudioMixerWasapiRunnable : public FRunnable
	{
	public:
		FAudioMixerWasapiRunnable() = delete;

		/**
		 * FAudioMixerWasapiRunnable
		 */
		explicit FAudioMixerWasapiRunnable(const TFunction<void()>& InDeviceRenderCallback, HANDLE& OutEventHandle);
		
		/** Default destructor */
		virtual ~FAudioMixerWasapiRunnable() = default;

		// Begin FRunnable overrides
		virtual uint32 Run() override;
		virtual void Stop() override;
		// End FRunnable overrides

	private:
		/** The main run loop for this runnable will continue iterating while this flag is true. */
		std::atomic<bool> bIsRunning;
		
		/**
		 * Event handle which our audio thread waits on prior to each callback. WASAPI signals this
		 * object each quanta when a buffer of audio has been rendered and is ready for more data.
		 */
		HANDLE EventHandle = nullptr;

		/**
		 * Accumulates timeouts which occur when the thread event timeout is reached
		 * prior to the event being signaled for new data being available.
		 */
		uint32 OutputStreamTimeoutsDetected = 0;
		
		/** Callback function to be called each time the device signals it is ready for another buffer of audio. */
		TFunction<void()> DeviceRenderCallback;
	};

	/**
	 * FAudioMixerWasapiDeviceThread - Manages both the FAudioMixerWasapiRunnable object and the thread whose context it runs in. 
	 */
	class FAudioMixerWasapiDeviceThread
	{
	public:
		FAudioMixerWasapiDeviceThread() = delete;

		/**
		 * FAudioMixerWasapiDeviceThread
		 */
		explicit FAudioMixerWasapiDeviceThread(const TFunction<void()>& InDeviceRenderCallback, HANDLE& OutEventHandle);

		/**
		 * Start - Creates the FRunnableThread object which immediately begins running the FAudioMixerWasapiRunnable member.
		 * 
		 * @return - Boolean indicating of the thread was succesfully created.
		 */
		bool Start();

		/**
		 * Stop - Gracefully shuts down the thread.
		 */
		void Stop();

		/**
		 * Abort - Performs non-graceful shutdown of thread which will close the underyling thread handle 
		 * without waiting for it to finish.
		 */
		void Abort();

	private:
		/** The thread which is the context that the runnable executes in. */
		TUniquePtr<FRunnableThread> DeviceRenderThread;

		/** The runnable which manages the run loop for the render stream. */
		TUniquePtr<FAudioMixerWasapiRunnable> DeviceRenderRunnable;
	};
}
