// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Delegates/Delegate.h"
#include "IPixelCaptureCapturerSource.h"
#include "PixelCaptureCapturerMultiFormat.h"
#include "VideoProducer.h"

#include "epic_rtc/core/video/video_buffer.h"

namespace UE::PixelStreaming2
{
	/**
	 * The start of PixelCapture pipeline. Frames enter the system when `OnFrameCaptured` is called.
	 * This class creates the underlying PixelCapture `FPixelCaptureCapturer` that handles frame capture when `RequestFormat` is called.
	 */
	class PIXELSTREAMING2_API FVideoCapturer : public IPixelCaptureCapturerSource, public TSharedFromThis<FVideoCapturer>
	{
	public:
		static TSharedPtr<FVideoCapturer> Create(TSharedPtr<FVideoProducer> VideoProducer = nullptr);
		virtual ~FVideoCapturer() = default;

		// Begin IPixelCaptureCapturerSource Interface
		virtual TSharedPtr<FPixelCaptureCapturer> CreateCapturer(int32 FinalFormat, float FinalScale) override;
		// End IPixelCaptureCapturerSource Interface

		bool IsReady() const { return bReady; }

		void SetVideoProducer(TSharedPtr<FVideoProducer> InVideoProducer);

		TSharedPtr<FVideoProducer> GetVideoProducer() { return VideoProducer; }

		TRefCountPtr<EpicRtcVideoBufferInterface> GetFrameBuffer();

		TSharedPtr<IPixelCaptureOutputFrame> RequestFormat(int32 Format, int32 LayerIndex = -1);

		/**
		 * This is broadcast each time a frame exits the adapt process. Used to synchronize framerates with input rates.
		 * This should be called once per frame taking into consideration all the target formats and layers within the frame.
		 */
		DECLARE_MULTICAST_DELEGATE(FOnFrameCaptured);
		FOnFrameCaptured OnFrameCaptured;

	private:
		FVideoCapturer(TSharedPtr<FVideoProducer> VideoProducer);

		int32 LastFrameWidth = -1;
		int32 LastFrameHeight = -1;
		int32 LastFrameType = -1;
		bool  bReady = false;

		TSharedPtr<FVideoProducer>					 VideoProducer;
		TSharedPtr<FPixelCaptureCapturerMultiFormat> FrameCapturer;
		FDelegateHandle								 CaptureCompleteHandle;
		FDelegateHandle								 SimulcastEnabledChangedHandle;
		FDelegateHandle								 CaptureUseFenceChangedHandle;
		FDelegateHandle								 FramePushedHandle;

		void CreateFrameCapturer();
		void OnSimulcastEnabledChanged(IConsoleVariable* Var);
		void OnCaptureUseFenceChanged(IConsoleVariable* Var);
		void OnCaptureComplete();
		void OnFrame(const IPixelCaptureInputFrame& InputFrame);
	};

} // namespace UE::PixelStreaming2
