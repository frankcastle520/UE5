// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IMediaTextureSample.h"

#include "IRivermaxInputStream.h"
#include "MediaIOCoreTextureSampleBase.h"
#include "MediaIOCoreSamples.h"
#include "MediaShaders.h"
#include "RivermaxMediaSource.h"
#include "Templates/SharedPointer.h"

class FEvent;

namespace UE::RivermaxMedia
{
	/**
	 * Implements a media texture sample for RivermaxMediaPlayer.  
	 */
	class FRivermaxMediaTextureSample : public FMediaIOCoreTextureSampleBase, public UE::RivermaxCore::IRivermaxVideoSample, public TSharedFromThis<FRivermaxMediaTextureSample>
	{
	public:
		FRivermaxMediaTextureSample();
		virtual ~FRivermaxMediaTextureSample() override;

		//~ Begin IMediaTextureSample interface
		virtual bool IsCacheable() const override;
		virtual const FMatrix& GetYUVToRGBMatrix() const override;

	#if WITH_ENGINE
		virtual IMediaTextureSampleConverter* GetMediaTextureSampleConverter() override;
	#endif
		//~ End IMediaTextureSample interface

		//~ Begin FMediaIOCoreTextureSampleBase interface
		virtual bool InitializeJITR(const FMediaIOCoreSampleJITRConfigurationArgs& Args) override;
		virtual void CopyConfiguration(const TSharedPtr<FMediaIOCoreTextureSampleBase>& SourceSample) override;
		//~ End FMediaIOCoreTextureSampleBase interface

		/** Initialized a RDG buffer based on the description required. Only useful for gpudirect functionality */
		void InitializeGPUBuffer(const FIntPoint& InResolution, ERivermaxMediaSourcePixelFormat InSampleFormat, bool bSupportsGPUDirect);

		/** Returns the incoming sample format */
		ERivermaxMediaSourcePixelFormat GetInputFormat() const;
		void SetInputFormat(ERivermaxMediaSourcePixelFormat InFormat);

		// Inherited via IRivermaxVideoSample
		virtual TRefCountPtr<FRDGPooledBuffer> GetGPUBuffer() const override;
		virtual uint8* GetVideoBufferRawPtr(uint32 VideoBufferSize) override;

		virtual void InitializePoolable() override {};
		virtual void ShutdownPoolable() override 
		{
			// This means that the sample is being held and managed by the Player
			FScopeLock Lock(&StateChangeCriticalSecion);

			// When this sample is returned back to the pool, it means that it is done rendering and is released from sample container.
			SetAwaitingForGPUTransfer(false);
			SetReceptionState(IRivermaxSample::ESampleState::ReadyForReception);
			if (SampleConversionFence.IsValid())
			{
				SampleConversionFence->Clear();
			}

			LockedMemory = nullptr;
			MarkRenderingComplete();
		};

		virtual bool IsReadyForReuse() override
		{
			return !IsBeingRendered();
		}

		/** 
		* Attempts to lock this sample for rendering. 
		* Returns true if sample is ok to be rendered. 
		* Returns false if sample is already being rendered. 
		*/
		bool TryLockForRendering()
		{
			FScopeLock Lock(&StateChangeCriticalSecion);
			if (bIsPendingRendering)
			{
				// sample is already being rendered.
				return false;
			}
			else
			{
				bIsPendingRendering = true;
				return true;
			}
		};

		/** 
		* Marks that this sample can be rendered again if need be.
		*/
		void MarkRenderingComplete()
		{
			FScopeLock Lock(&StateChangeCriticalSecion);
			bIsPendingRendering = false;
		};

		bool IsBeingRendered()
		{
			FScopeLock Lock(&StateChangeCriticalSecion);
			return bIsPendingRendering;
		};

		FEvent* GetSampleReceivedEvent() 
		{
			return SampleReceivedEvent;
		}

	public:
		/** Locked memory of gpu buffer when uploading */
		void* LockedMemory = nullptr;

		/** Write fence enqueued after sample conversion to know when it's ready to be reused */
		FGPUFenceRHIRef SampleConversionFence;

	private:
		/** This event isued to wait for the sample to be fully received.*/
		FEvent* SampleReceivedEvent = nullptr;

		/** True when queued for rendering. Will be false once fence has been written, after shader usage. */
		std::atomic<bool> bIsPendingRendering = false;

	private:

		/** Format in the rivermax realm */
		ERivermaxMediaSourcePixelFormat InputFormat = ERivermaxMediaSourcePixelFormat::YUV422_8bit;

		/** Texture stride */
		uint32 Stride = 0;

		/** Pooled buffer used for gpudirect functionality. Received content will already be on GPU when received from NIC */
		TRefCountPtr<FRDGPooledBuffer> GPUBuffer;

	private:
		friend class FRivermaxMediaPlayer;

		/** The start of the reception marked by the first chunk received by rivermax. */
		FTimespan FrameReceptionStart = 0;

		/** The end of the reception marked by the last processed packet. */
		FTimespan FrameReceptionEnd = 0;
	};

	class FRivermaxMediaTextureSamplePool : public TMediaObjectPool<FRivermaxMediaTextureSample> { };
}

