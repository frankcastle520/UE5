// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Event.h"
#include "HAL/IConsoleManager.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Misc/SingleThreadRunnable.h"
#include "VideoCapturer.h"
#include "Templates/SharedPointer.h"

namespace UE::PixelStreaming2
{
	class FEpicRtcVideoSource;
	class FFrameRunnable;

	class FVideoSourceGroup : public TSharedFromThis<FVideoSourceGroup>
	{
	public:
		static TSharedPtr<FVideoSourceGroup> Create(TSharedPtr<FVideoCapturer> InVideoCapturer);
		~FVideoSourceGroup();

		void  SetFPS(int32 InFramesPerSecond);
		int32 GetFPS();

		void SetDecoupleFramerate(bool bDecouple);

		void AddVideoSource(TSharedPtr<FEpicRtcVideoSource> VideoSource);
		void RemoveVideoSource(const FEpicRtcVideoSource* ToRemove);
		void RemoveAllVideoSources();

		void Start();
		void Stop();
		void PushFrame();

		void ForceKeyFrame();

	private:
		FVideoSourceGroup();

		void StartThread();
		void StopThread();

		void OnFrameCaptured();

		void OnWebRtcFpsChanged(IConsoleVariable* Var);
		void OnDecoupleFramerateChanged(IConsoleVariable* Var);

		bool bFPSOverride = false;
		bool bDecoupleOverride = false;

		bool  bRunning = false;
		bool  bDecoupleFramerate = false;
		int32 FramesPerSecond = 30;

		TArray<TSharedPtr<FEpicRtcVideoSource>> VideoSources;

		FDelegateHandle FrameDelegateHandle;
		FDelegateHandle FpsDelegateHandle;
		FDelegateHandle DecoupleDelegateHandle;

		TSharedPtr<FRunnableThread> FrameThread = nullptr;	 // constant FPS tick thread
		TSharedPtr<FFrameRunnable>	FrameRunnable = nullptr; // constant fps runnable

		mutable FCriticalSection CriticalSection;
		mutable FCriticalSection RunnableCriticalSection;
	};
} // namespace UE::PixelStreaming2
