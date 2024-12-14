// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "Templates/UnrealTemplate.h"

class UVCamPixelStreamingSession;
class UVCamPixelStreamingSubsystem;

namespace UE::PixelStreamingVCam
{
	/**
	 * Controls the lifetime of the builtin signalling server for Pixel Streaming.
	 *
	 * Generally ensures that the server is launched when the first FVCamPixelStreamingSessionLogic launches and that it is shutdown when the last
	 * FVCamPixelStreamingSessionLogic shuts down.
	 * If the server was already running before the first FVCamPixelStreamingSessionLogic was launched, it stays alive.
	 *
	 * This logic does not handle the case of when the server is lost during streaming and manually relaunched. It will either shutdown or keep the
	 * server alive depending on whether there was one running before the first session started.
	 */
	class FSignalingServerLifecycle : public FNoncopyable
	{
	public:
		
		FSignalingServerLifecycle(UVCamPixelStreamingSubsystem& Subsystem UE_LIFETIMEBOUND);

		/** Called when a streamer requires a signalling server. */
		void LaunchSignallingServerIfNeeded(UVCamPixelStreamingSession& Session);
		/** Called when a  */
		void StopSignallingServerIfNeeded(UVCamPixelStreamingSession& Session);

	private:

		/** Notifies us when a VCam session starts / stops. */
		UVCamPixelStreamingSubsystem& Subsystem;

		enum class ELifecycleState
		{
			/** There's nobody streaming and the server is no longer managed by us. */
			NoClients,
			/** We manually launched a server for VCam. Shut it down when the last session stops streaming. */
			ShutdownOnLastSession,
			/** There was a server that had been launched before the first session started streaming. Do not shutdown the server when the last session stops streaming. */
			KeepAliveOnLastSession
		} LifecycleState = ELifecycleState::NoClients;
	};
}

