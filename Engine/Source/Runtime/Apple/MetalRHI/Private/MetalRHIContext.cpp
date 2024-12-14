// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetalRHIContext.h"
#include "MetalRHIPrivate.h"
#include "MetalRHIRenderQuery.h"
#include "MetalRHIVisionOSBridge.h"
#include "MetalBindlessDescriptors.h"
#include "MetalDevice.h"
#include "MetalCommandBuffer.h"
#include "MetalDynamicRHI.h"

#if PLATFORM_VISIONOS
#import <CompositorServices/CompositorServices.h>
#endif

void METALRHI_API SafeReleaseMetalObject(NS::Object* Object)
{
	if(GIsMetalInitialized && GDynamicRHI && Object)
	{
		if(!IsRunningRHIInSeparateThread())
		{
			FMetalDynamicRHI::Get().DeferredDelete(Object);
		}
		else
		{
			FFunctionGraphTask::CreateAndDispatchWhenReady(
			   [Object]()
			   {
				   FMetalDynamicRHI::Get().DeferredDelete(Object);
			   },
			   QUICK_USE_CYCLE_STAT(FExecuteRHIThreadTask, STATGROUP_TaskGraphTasks), nullptr, ENamedThreads::RHIThread);
		}
		
		return;
	}
	Object->release();
}

FMetalRHICommandContext::FMetalRHICommandContext(FMetalDevice& MetalDevice, class FMetalProfiler* InProfiler)
	: Device(MetalDevice)
	, CommandQueue(Device.GetCommandQueue())
	, CommandList(Device.GetCommandQueue())
	, CurrentEncoder(MetalDevice, CommandList)
	, StateCache(MetalDevice, true)
	, QueryBuffer(new FMetalQueryBufferPool(MetalDevice))
	, RenderPassDesc(nullptr)
	, Profiler(InProfiler)
	, bWithinRenderPass(false)
{
	GlobalUniformBuffers.AddZeroed(FUniformBufferStaticSlotRegistry::Get().GetSlotCount());
}

FMetalRHICommandContext::~FMetalRHICommandContext()
{
	CurrentEncoder.Release();
	Device.WaitForGPUIdle();
}

void FMetalRHICommandContext::ResetContext()
{
	// Reset cached state in the encoder
	StateCache.Reset();
	
	// Reset the current encoder
	CurrentEncoder.Reset();
	
	// Reallocate if necessary to ensure >= 80% usage, otherwise we're just too wasteful
	CurrentEncoder.GetRingBuffer().Shrink();
	
	// Begin the render pass frame.
	CurrentEncoder.StartCommandBuffer();
	
	// make sure first SetRenderTarget goes through
	StateCache.InvalidateRenderTargets();
}

static uint32_t MAX_COLOR_RENDER_TARGETS_PER_DESC = 8;

void FMetalRHICommandContext::BeginComputeEncoder()
{
	SCOPE_CYCLE_COUNTER(STAT_MetalSwitchToComputeTime);
	
	check(!bWithinRenderPass);
	check(CurrentEncoder.GetCommandBuffer());
	check(IsInParallelRenderingThread());
	
	StateCache.SetStateDirty();
	
	if(!CurrentEncoder.IsComputeCommandEncoderActive())
	{
		StateCache.ClearPreviousComputeState();
		if(CurrentEncoder.IsAnyCommandEncoderActive())
		{
			CurrentEncoderFence = CurrentEncoder.EndEncoding();
		}
		CurrentEncoder.BeginComputeCommandEncoding(MTL::DispatchTypeSerial);
	}
	
	if (CurrentEncoderFence)
	{
		CurrentEncoder.WaitForFence(CurrentEncoderFence);
		CurrentEncoderFence = nullptr;
	}
	
	check(CurrentEncoder.IsComputeCommandEncoderActive());
}

void FMetalRHICommandContext::EndComputeEncoder()
{
	check(CurrentEncoder.IsComputeCommandEncoderActive());
	
	StateCache.SetRenderTargetsActive(false);
}

void FMetalRHICommandContext::BeginBlitEncoder()
{
	SCOPE_CYCLE_COUNTER(STAT_MetalSwitchToBlitTime);
	check(!bWithinRenderPass);
	check(CurrentEncoder.GetCommandBuffer());
	
	if(!CurrentEncoder.IsBlitCommandEncoderActive())
	{
		if(CurrentEncoder.IsAnyCommandEncoderActive())
		{
			CurrentEncoderFence = CurrentEncoder.EndEncoding();
		}
		CurrentEncoder.BeginBlitCommandEncoding();
	}
	
	if (CurrentEncoderFence)
	{
		CurrentEncoder.WaitForFence(CurrentEncoderFence);
		CurrentEncoderFence = nullptr;
	}
	
	check(CurrentEncoder.IsBlitCommandEncoderActive());
}

void FMetalRHICommandContext::EndBlitEncoder()
{
	check(CurrentEncoder.IsBlitCommandEncoderActive());
	
	StateCache.SetRenderTargetsActive(false);
}

void FMetalRHICommandContext::RHIBeginRenderPass(const FRHIRenderPassInfo& InInfo, const TCHAR* InName)
{	
    MTL_SCOPED_AUTORELEASE_POOL;
	
#if PLATFORM_SUPPORTS_BINDLESS_RENDERING
	if(IsMetalBindlessEnabled())
	{
		check(!bWithinRenderPass);
		Device.GetBindlessDescriptorManager()->UpdateDescriptorsWithGPU(this);
	}
#endif
    
	RenderPassInfo = InInfo;
	
	if (InInfo.NumOcclusionQueries > 0)
	{
		RHIBeginOcclusionQueryBatch(InInfo.NumOcclusionQueries);
	}
	
	if (!CurrentEncoder.GetCommandBuffer())
	{
		CurrentEncoder.StartCommandBuffer();
		check(CurrentEncoder.GetCommandBuffer());
	}
	
	StateCache.SetStateDirty();
	StateCache.SetRenderTargetsActive(true);
	StateCache.StartRenderPass(InInfo, QueryBuffer->GetCurrentQueryBuffer());
	
	RenderPassDesc = StateCache.GetRenderPassDescriptor();
	
	check(IsInParallelRenderingThread());
	
	if(!CurrentEncoder.IsRenderCommandEncoderActive())
	{
		if(CurrentEncoder.IsAnyCommandEncoderActive())
		{
			CurrentEncoderFence = CurrentEncoder.EndEncoding();
		}
		CurrentEncoder.SetRenderPassDescriptor(RenderPassDesc);
		CurrentEncoder.BeginRenderCommandEncoding();
	}
	
	if (CurrentEncoderFence)
	{
		CurrentEncoder.WaitForFence(CurrentEncoderFence);
		CurrentEncoderFence = nullptr;
	}
	StateCache.SetRenderStoreActions(CurrentEncoder, false);
	check(CurrentEncoder.IsRenderCommandEncoderActive());

	bWithinRenderPass = true;
	
	// Set the viewport to the full size of render target 0.
	if (InInfo.ColorRenderTargets[0].RenderTarget)
	{
		const FRHIRenderPassInfo::FColorEntry& RenderTargetView = InInfo.ColorRenderTargets[0];
		FMetalSurface* RenderTarget = GetMetalSurfaceFromRHITexture(RenderTargetView.RenderTarget);

		uint32 Width = FMath::Max((uint32)(RenderTarget->Texture->width() >> RenderTargetView.MipIndex), (uint32)1);
		uint32 Height = FMath::Max((uint32)(RenderTarget->Texture->height() >> RenderTargetView.MipIndex), (uint32)1);

		RHISetViewport(0.0f, 0.0f, 0.0f, (float)Width, (float)Height, 1.0f);
	}
}

void FMetalRHICommandContext::RHIEndRenderPass()
{
	if (RenderPassInfo.NumOcclusionQueries > 0)
	{
		RHIEndOcclusionQueryBatch();
	}
    
	check(bWithinRenderPass);
	check(CurrentEncoder.IsRenderCommandEncoderActive());
	
	StateCache.FlushVisibilityResults(CurrentEncoder);
	
	CurrentEncoderFence = CurrentEncoder.EndEncoding();
	bWithinRenderPass = false;
	
	// Uses a Blit encoder so need to run after end encoding 
	UE::RHICore::ResolveRenderPassTargets(RenderPassInfo, [this](UE::RHICore::FResolveTextureInfo Info)
	{
		ResolveTexture(Info);
	});
	
	StateCache.SetRenderTargetsActive(false);
	RenderPassDesc = nullptr;
}

void FMetalRHICommandContext::ResolveTexture(UE::RHICore::FResolveTextureInfo Info)
{
    MTL_SCOPED_AUTORELEASE_POOL;
    
	FMetalSurface* Source = GetMetalSurfaceFromRHITexture(Info.SourceTexture);
	FMetalSurface* Destination = GetMetalSurfaceFromRHITexture(Info.DestTexture);

	const FRHITextureDesc& SourceDesc = Source->GetDesc();
	const FRHITextureDesc& DestinationDesc = Destination->GetDesc();

	const bool bDepthStencil = SourceDesc.Format == PF_DepthStencil;
	const bool bSupportsMSAADepthResolve = Device.SupportsFeature(EMetalFeaturesMSAADepthResolve);
	const bool bSupportsMSAAStoreAndResolve = Device.SupportsFeature(EMetalFeaturesMSAAStoreAndResolve);
	// Resolve required - Device must support this - Using Shader for resolve not supported amd NumSamples should be 1
	check((!bDepthStencil && bSupportsMSAAStoreAndResolve) || (bDepthStencil && bSupportsMSAADepthResolve));

	MTL::Origin Origin(0, 0, 0);
    MTL::Size Size(0, 0, 1);

	if (Info.ResolveRect.IsValid())
	{
		Origin.x    = Info.ResolveRect.X1;
		Origin.y    = Info.ResolveRect.Y1;
		Size.width  = Info.ResolveRect.X2 - Info.ResolveRect.X1;
		Size.height = Info.ResolveRect.Y2 - Info.ResolveRect.Y1;
	}
	else
	{
		Size.width  = FMath::Max<uint32>(1, SourceDesc.Extent.X >> Info.MipLevel);
		Size.height = FMath::Max<uint32>(1, SourceDesc.Extent.Y >> Info.MipLevel);
	}

	if (Profiler)
	{
		Profiler->RegisterGPUWork();
	}

	int32 ArraySliceBegin = Info.ArraySlice;
	int32 ArraySliceEnd   = Info.ArraySlice + 1;

	if (Info.ArraySlice < 0)
	{
		ArraySliceBegin = 0;
		ArraySliceEnd   = SourceDesc.ArraySize;
	}

	BeginBlitEncoder();
	
	MTL::BlitCommandEncoder* Encoder = CurrentEncoder.GetBlitCommandEncoder();
	
	check(Encoder);
	
	for (int32 ArraySlice = ArraySliceBegin; ArraySlice < ArraySliceEnd; ArraySlice++)
	{
		METAL_GPUPROFILE(FMetalProfiler::GetProfiler()->EncodeBlit(CurrentEncoder.GetCommandBufferStats(), __FUNCTION__));
		Encoder->copyFromTexture(Source->MSAAResolveTexture.get(), ArraySlice, Info.MipLevel, Origin, Size, Destination->Texture.get(), ArraySlice, Info.MipLevel, Origin);
	}
	
	EndBlitEncoder();
}

void FMetalRHICommandContext::RHINextSubpass()
{
#if PLATFORM_MAC
	if (RenderPassInfo.SubpassHint == ESubpassHint::DepthReadSubpass)
	{
		if (CurrentEncoder.IsRenderCommandEncoderActive())
		{
			MTL::RenderCommandEncoder* RenderEncoder = CurrentEncoder.GetRenderCommandEncoder();
			check(RenderEncoder);
			RenderEncoder->memoryBarrier(MTL::BarrierScopeRenderTargets, MTL::RenderStageFragment, MTL::RenderStageVertex);
		}
	}
#endif
}

void FMetalRHICommandContext::RHICalibrateTimers(FRHITimestampCalibrationQuery* CalibrationQuery)
{
    MTL::Device* MTLDevice = Device.GetDevice();
    
    MTL::Timestamp CPUTimeStamp, GPUTimestamp;
    MTLDevice->sampleTimestamps(&CPUTimeStamp, &GPUTimestamp);

    CalibrationQuery->CPUMicroseconds[0] = uint64(CPUTimeStamp / 1000.0);
    CalibrationQuery->GPUMicroseconds[0] = uint64(GPUTimestamp / 1000.0);
}

void FMetalDynamicRHI::RHIBeginRenderQuery_TopOfPipe(FRHICommandListBase& RHICmdList, FRHIRenderQuery* RenderQuery)
{
	FMetalRHIRenderQuery* Query = ResourceCast(RenderQuery);
	Query->Begin_TopOfPipe();

	FDynamicRHI::RHIBeginRenderQuery_TopOfPipe(RHICmdList, RenderQuery);
}

void FMetalDynamicRHI::RHIEndRenderQuery_TopOfPipe(FRHICommandListBase& RHICmdList, FRHIRenderQuery* RenderQuery)
{
	FMetalRHIRenderQuery* Query = ResourceCast(RenderQuery);
	Query->End_TopOfPipe();

	FDynamicRHI::RHIEndRenderQuery_TopOfPipe(RHICmdList, RenderQuery);
}

void FMetalRHICommandContext::RHIBeginRenderQuery(FRHIRenderQuery* QueryRHI)
{
    MTL_SCOPED_AUTORELEASE_POOL;
	FMetalRHIRenderQuery* Query = ResourceCast(QueryRHI);
    Query->Begin(this, CommandBufferFence);
}

void FMetalRHICommandContext::RHIEndRenderQuery(FRHIRenderQuery* QueryRHI)
{
    MTL_SCOPED_AUTORELEASE_POOL;
    FMetalRHIRenderQuery* Query = ResourceCast(QueryRHI);
	Query->End(this);
}

void FMetalRHICommandContext::RHIBeginOcclusionQueryBatch(uint32 NumQueriesInBatch)
{
    check(!CommandBufferFence.IsValid());
	CommandBufferFence = MakeShareable(new FMetalCommandBufferFence);
    CurrentEncoder.InsertCommandBufferFence(CommandBufferFence, FMetalCommandBufferCompletionHandler());
}

void FMetalRHICommandContext::RHIEndOcclusionQueryBatch()
{
	check(CommandBufferFence.IsValid());
	CommandBufferFence.Reset();
}

void FMetalRHICommandContext::FillBuffer(MTL::Buffer* Buffer, NS::Range Range, uint8 Value)
{
	check(Buffer);
	
	MTL::BlitCommandEncoder *TargetEncoder;
	
	BeginBlitEncoder();
	TargetEncoder = CurrentEncoder.GetBlitCommandEncoder();
	METAL_GPUPROFILE(FMetalProfiler::GetProfiler()->EncodeBlit(CurrentEncoder.GetCommandBufferStats(), FString::Printf(TEXT("FillBuffer: %p %llu %llu"), Buffer, Range.location, Range.length)));
	
	check(TargetEncoder);
	
	TargetEncoder->fillBuffer(Buffer, Range, Value);
	
	EndBlitEncoder();
}

void FMetalRHICommandContext::CopyFromTextureToBuffer(MTL::Texture* Texture, uint32 sourceSlice, uint32 sourceLevel, MTL::Origin sourceOrigin, MTL::Size sourceSize, FMetalBufferPtr toBuffer, uint32 destinationOffset, uint32 destinationBytesPerRow, uint32 destinationBytesPerImage, MTL::BlitOption options)
{
	BeginBlitEncoder();
	MTL::BlitCommandEncoder* Encoder = CurrentEncoder.GetBlitCommandEncoder();
	check(Encoder);
	
	METAL_GPUPROFILE(FMetalProfiler::GetProfiler()->EncodeBlit(CurrentEncoder.GetCommandBufferStats(), __FUNCTION__));
	{
		if(Texture)
		{
			Encoder->copyFromTexture(Texture, sourceSlice, sourceLevel, sourceOrigin, sourceSize,
									 toBuffer->GetMTLBuffer(), destinationOffset + toBuffer->GetOffset(), destinationBytesPerRow, destinationBytesPerImage, options);
		}
	}
	EndBlitEncoder();
}

void FMetalRHICommandContext::CopyFromBufferToTexture(FMetalBufferPtr Buffer, uint32 sourceOffset, uint32 sourceBytesPerRow, uint32 sourceBytesPerImage, MTL::Size sourceSize, MTL::Texture* toTexture, uint32 destinationSlice, uint32 destinationLevel, MTL::Origin destinationOrigin, MTL::BlitOption options)
{
	BeginBlitEncoder();
	MTL::BlitCommandEncoder* Encoder = CurrentEncoder.GetBlitCommandEncoder();
	check(Encoder);
	
	METAL_GPUPROFILE(FMetalProfiler::GetProfiler()->EncodeBlit(CurrentEncoder.GetCommandBufferStats(), __FUNCTION__));
	if (options == MTL::BlitOptionNone)
	{
		Encoder->copyFromBuffer(Buffer->GetMTLBuffer(), sourceOffset + Buffer->GetOffset(), sourceBytesPerRow, sourceBytesPerImage, sourceSize,
								toTexture, destinationSlice, destinationLevel, destinationOrigin);
	}
	else
	{
		Encoder->copyFromBuffer(Buffer->GetMTLBuffer(), sourceOffset + Buffer->GetOffset(), sourceBytesPerRow, sourceBytesPerImage, sourceSize,
								toTexture, destinationSlice, destinationLevel, destinationOrigin, options);
	}
	
	EndBlitEncoder();
}

void FMetalRHICommandContext::CopyFromTextureToTexture(MTL::Texture* Texture, uint32 sourceSlice, uint32 sourceLevel, MTL::Origin sourceOrigin, MTL::Size sourceSize, MTL::Texture* toTexture, uint32 destinationSlice, uint32 destinationLevel, MTL::Origin destinationOrigin)
{
	BeginBlitEncoder();
	
	MTL::BlitCommandEncoder* Encoder = CurrentEncoder.GetBlitCommandEncoder();
	check(Encoder);
	
	METAL_GPUPROFILE(FMetalProfiler::GetProfiler()->EncodeBlit(CurrentEncoder.GetCommandBufferStats(), __FUNCTION__));
	Encoder->copyFromTexture(Texture, sourceSlice, sourceLevel, sourceOrigin, sourceSize, toTexture, destinationSlice, destinationLevel, destinationOrigin);
	
	EndBlitEncoder();
}

void FMetalRHICommandContext::CopyFromBufferToBuffer(FMetalBufferPtr SourceBuffer, NS::UInteger SourceOffset, FMetalBufferPtr DestinationBuffer, NS::UInteger DestinationOffset, NS::UInteger Size)
{
	BeginBlitEncoder();
	
	MTL::BlitCommandEncoder* Encoder = CurrentEncoder.GetBlitCommandEncoder();
	check(Encoder);
	
	METAL_GPUPROFILE(FMetalProfiler::GetProfiler()->EncodeBlit(CurrentEncoder.GetCommandBufferStats(), __FUNCTION__));
	
	Encoder->copyFromBuffer(SourceBuffer->GetMTLBuffer(), SourceOffset + SourceBuffer->GetOffset(),
							DestinationBuffer->GetMTLBuffer(), DestinationOffset + DestinationBuffer->GetOffset(), Size);
	
	EndBlitEncoder();
}

TArray<FMetalCommandBuffer*> FMetalRHICommandContext::Finalize()
{
	GetQueryBufferPool()->ReleaseCurrentQueryBuffer();
	
	if(CurrentEncoder.IsAnyCommandEncoderActive())
	{
		CurrentEncoderFence = CurrentEncoder.EndEncoding();
	}
	
	TArray<FMetalCommandBuffer*> CommandBuffers;
	
	if (CurrentEncoder.GetCommandBuffer())
	{
		CommandBuffers = CurrentEncoder.Finalize();
	}
	
	return CommandBuffers;
}

void FMetalRHICommandContext::InsertCommandBufferFence(TSharedPtr<FMetalCommandBufferFence, ESPMode::ThreadSafe>& Fence, FMetalCommandBufferCompletionHandler Handler)
{
	CurrentEncoder.InsertCommandBufferFence(Fence, Handler);
}

void FMetalRHICommandContext::SignalEvent(MTLEventPtr Event, uint32_t SignalCount)
{
	CurrentEncoder.SignalEvent(Event, SignalCount);
}

void FMetalRHICommandContext::WaitForEvent(MTLEventPtr Event, uint32_t SignalCount)
{
	CurrentEncoder.WaitForEvent(Event, SignalCount);
}

void FMetalRHICommandContext::StartTiming(class FMetalEventNode* EventNode)
{
	FMetalCommandBufferCompletionHandler Handler;
	
	bool const bHasCurrentCommandBuffer = CurrentEncoder.GetCommandBuffer();
	
	if(EventNode)
	{
		Handler = EventNode->Start();
		
		if (bHasCurrentCommandBuffer)
		{
			CurrentEncoder.AddCompletionHandler(Handler);
		}
		
		if(!bWithinRenderPass)
		{
			CurrentEncoder.SplitCommandBuffers();
		}
	}
	
	if (Handler.IsBound() && !bHasCurrentCommandBuffer)
	{
		CurrentEncoder.GetCommandBuffer()->GetMTLCmdBuffer()->addScheduledHandler(
						MTL::HandlerFunction([Handler](MTL::CommandBuffer* CommandBuffer)
						{
							Handler.Execute(CommandBuffer);
						}));
	}
}

void FMetalRHICommandContext::EndTiming(class FMetalEventNode* EventNode)
{
	FMetalCommandBufferCompletionHandler Handler = EventNode->Stop();
	CurrentEncoder.AddCompletionHandler(Handler);
}

void FMetalRHICommandContext::SynchronizeResource(MTL::Resource* Resource)
{
	check(Resource);
#if PLATFORM_MAC
	BeginBlitEncoder();
	MTL::BlitCommandEncoder* Encoder = CurrentEncoder.GetBlitCommandEncoder();
	check(Encoder);
	
	METAL_GPUPROFILE(FMetalProfiler::GetProfiler()->EncodeBlit(CurrentEncoder.GetCommandBufferStats(), __FUNCTION__));
	Encoder->synchronizeResource(Resource);
	EndBlitEncoder();
#endif
}

void FMetalRHICommandContext::SynchronizeTexture(MTL::Texture* Texture, uint32 Slice, uint32 Level)
{
	check(Texture);
#if PLATFORM_MAC
	BeginBlitEncoder();
	MTL::BlitCommandEncoder* Encoder = CurrentEncoder.GetBlitCommandEncoder();
	check(Encoder);
	
	METAL_GPUPROFILE(FMetalProfiler::GetProfiler()->EncodeBlit(CurrentEncoder.GetCommandBufferStats(), __FUNCTION__));
	Encoder->synchronizeTexture(Texture, Slice, Level);
	EndBlitEncoder();
#endif
}

void FMetalRHICommandContext::AddCompletionHandler(FMetalCommandBufferCompletionHandler& Handler)
{
	CurrentEncoder.AddCompletionHandler(Handler);
}

FMetalCommandBuffer* FMetalRHICommandContext::GetCurrentCommandBuffer()
{
	FMetalCommandBuffer* CommandBuffer = CurrentEncoder.GetCommandBuffer();
	if(!CommandBuffer)
	{
		CurrentEncoder.StartCommandBuffer();
	}
	return CurrentEncoder.GetCommandBuffer();
}

FMetalRHIUploadContext::FMetalRHIUploadContext(FMetalDevice& Device)
{
	UploadContext = new FMetalRHICommandContext(Device, nullptr);
	UploadContext->ResetContext();
	
	WaitContext = new FMetalRHICommandContext(Device, nullptr);
	WaitContext->ResetContext();
	
	UploadSyncEvent = Device.CreateEvent();
}

FMetalRHIUploadContext::~FMetalRHIUploadContext()
{
	delete UploadContext;
	delete WaitContext;
}

TArray<FMetalCommandBuffer*>* FMetalRHIUploadContext::Finalize()
{
	for(auto& Function : UploadFunctions)
	{
		Function(UploadContext);
	}
	
	UploadSyncCounter++;
	UploadContext->SignalEvent(UploadSyncEvent, UploadSyncCounter);
	
	TArray<FMetalCommandBuffer*>* CommandBuffers = new TArray<FMetalCommandBuffer*>();
	
	CommandBuffers->Append(UploadContext->Finalize());
	
	UploadFunctions.Reset();
	UploadContext->ResetContext();
	
	WaitContext->WaitForEvent(UploadSyncEvent, UploadSyncCounter);
	CommandBuffers->Append(WaitContext->Finalize());
	
	WaitContext->ResetContext();
	
	return CommandBuffers;
}

FMetalContextArray::FMetalContextArray(FRHIContextArray const& Contexts)
	: TRHIPipelineArray(InPlace, nullptr)
{
	for (ERHIPipeline Pipeline : MakeFlagsRange(ERHIPipeline::All))
	{
		IRHIComputeContext* Context = Contexts[Pipeline];

		switch (Pipeline)
		{
		default:
			checkNoEntry();
			break;

		case ERHIPipeline::Graphics:
			(*this)[Pipeline] = Context ? static_cast<FMetalRHICommandContext*>(&Context->GetLowestLevelContext()) : nullptr;
			break;

		case ERHIPipeline::AsyncCompute:
			(*this)[Pipeline] = Context ? static_cast<FMetalRHICommandContext*>(&Context->GetLowestLevelContext()) : nullptr;
			break;
		}
	}
}
