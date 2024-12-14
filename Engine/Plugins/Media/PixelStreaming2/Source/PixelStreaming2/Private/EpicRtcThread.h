// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace UE::PixelStreaming2
{
	/**
	 * The thread. Wraps both the RunnableThread and Runnable into a single point
	 */
	class FEpicRtcThread
	{
	public:
		FEpicRtcThread();
		~FEpicRtcThread();

	private:
		TSharedPtr<class FRunnableThread>  Thread;
		TSharedPtr<class FEpicRtcRunnable> Runnable;
	};
} // namespace UE::PixelStreaming2
