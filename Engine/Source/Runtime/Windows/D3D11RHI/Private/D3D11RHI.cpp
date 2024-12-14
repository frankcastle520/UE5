// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	D3D11RHI.cpp: Unreal D3D RHI library implementation.
=============================================================================*/

#include "D3D11RHI.h"
#include "D3D11RHIPrivate.h"
#include "RHIStaticStates.h"
#include "StaticBoundShaderState.h"
#include "Engine/GameViewportClient.h"
#include "ProfilingDebugging/MemoryTrace.h"
#include "RHICoreStats.h"

#if WITH_DX_PERF
	// For perf events
	#include "Windows/AllowWindowsPlatformTypes.h"
		#include "d3d9.h"
	#include "Windows/HideWindowsPlatformTypes.h"
#endif	//WITH_DX_PERF
#include "OneColorShader.h"

DEFINE_LOG_CATEGORY(LogD3D11RHI);

extern void UniformBufferBeginFrame();

// http://developer.download.nvidia.com/devzone/devcenter/gamegraphics/files/OptimusRenderingPolicies.pdf
// The following line is to favor the high performance NVIDIA GPU if there are multiple GPUs
// Has to be .exe module to be correctly detected.
// extern "C" { _declspec(dllexport) uint32 NvOptimusEnablement = 0x00000001; }

void FD3D11DynamicRHI::RHIEndFrame(const FRHIEndFrameArgs& Args)
{
	// End Frame
#if RHI_NEW_GPU_PROFILER
	{
		// End GPU work
		auto& EndWork = EmplaceProfilerEvent<UE::RHI::GPUProfiler::FEvent::FEndWork>();
		InsertProfilerTimestamp(&EndWork.GPUTimestampBOP);

		// Insert frame boundary
		EmplaceProfilerEvent<UE::RHI::GPUProfiler::FEvent::FFrameBoundary>(Args.FrameNumber
	#if WITH_RHI_BREADCRUMBS
			, Args.GPUBreadcrumbs[ERHIPipeline::Graphics]
	#endif
		);

		// Issue a completion query so we know when to readback these profiler results.
		{
			if (Profiler.EventPool.IsEmpty())
			{
				D3D11_QUERY_DESC QueryDesc {};
				QueryDesc.Query = D3D11_QUERY_EVENT;
				VERIFYD3D11RESULT(Direct3DDevice->CreateQuery(&QueryDesc, Profiler.Current.CompletionQuery.GetInitReference()));
			}
			else
			{
				Profiler.Current.CompletionQuery = Profiler.EventPool.Pop();
			}

			Direct3DDeviceIMContext->End(Profiler.Current.CompletionQuery);
			Profiler.Pending.Enqueue(MakeUnique<FProfiler::FFrame>(MoveTemp(Profiler.Current)));
		}

		// Attempt to process historic results
		while (TUniquePtr<FProfiler::FFrame>* PreviousFramePtr = Profiler.Pending.Peek())
		{
			TUniquePtr<FProfiler::FFrame>& PreviousFrame = *PreviousFramePtr;

			BOOL EventComplete = false;
			VERIFYD3D11RESULT(Direct3DDeviceIMContext->GetData(PreviousFrame->CompletionQuery, &EventComplete, sizeof(EventComplete), 0));
			if (!EventComplete)
			{
				// Frame not yet finished on the GPU
				break;
			}

			// Ensure we have the latest timestamp data
			PollQueryResults();

			// Previous frame has completed and the data is available. Publish the profiler events.
			UE::RHI::GPUProfiler::ProcessEvents(UE::RHI::GPUProfiler::FQueue(UE::RHI::GPUProfiler::FQueue::EType::Graphics, 0, 0), MoveTemp(PreviousFrame->EventStream));

			Profiler.EventPool.Push(MoveTemp(PreviousFrame->CompletionQuery));
			Profiler.Pending.Pop();
		}

		// Start the next frame's GPU work
		auto& BeginWork = EmplaceProfilerEvent<UE::RHI::GPUProfiler::FEvent::FBeginWork>(FPlatformTime::Cycles64());
		InsertProfilerTimestamp(&BeginWork.GPUTimestampTOP);
	}

#else
	GPUProfilingData.EndFrame();
#endif

	UpdateMemoryStats();
	CurrentComputeShader = nullptr;

	// Begin Frame
	UniformBufferBeginFrame();
#if (RHI_NEW_GPU_PROFILER == 0)
	GPUProfilingData.BeginFrame(this);
#endif
}

template <int32 Frequency>
void ClearShaderResource(ID3D11DeviceContext* Direct3DDeviceIMContext, uint32 ResourceIndex)
{
	ID3D11ShaderResourceView* NullView = NULL;
	switch(Frequency)
	{
	case SF_Pixel:   Direct3DDeviceIMContext->PSSetShaderResources(ResourceIndex,1,&NullView); break;
	case SF_Compute: Direct3DDeviceIMContext->CSSetShaderResources(ResourceIndex,1,&NullView); break;
	case SF_Geometry:Direct3DDeviceIMContext->GSSetShaderResources(ResourceIndex,1,&NullView); break;
	case SF_Vertex:  Direct3DDeviceIMContext->VSSetShaderResources(ResourceIndex,1,&NullView); break;
	};
}

void FD3D11DynamicRHI::ClearState()
{
	StateCache.ClearState();

	FMemory::Memzero(CurrentResourcesBoundAsSRVs, sizeof(CurrentResourcesBoundAsSRVs));
	FMemory::Memzero(CurrentResourcesBoundAsVBs, sizeof(CurrentResourcesBoundAsVBs));
	CurrentResourceBoundAsIB = nullptr;
	for (int32 Frequency = 0; Frequency < SF_NumStandardFrequencies; Frequency++)
	{
		MaxBoundShaderResourcesIndex[Frequency] = INDEX_NONE;
	}
	MaxBoundVertexBufferIndex = INDEX_NONE;
}

void GetMipAndSliceInfoFromSRV(ID3D11ShaderResourceView* SRV, int32& MipLevel, int32& NumMips, int32& ArraySlice, int32& NumSlices)
{
	MipLevel = -1;
	NumMips = -1;
	ArraySlice = -1;
	NumSlices = -1;

	if (SRV)
	{
		D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc;
		SRV->GetDesc(&SRVDesc);
		switch (SRVDesc.ViewDimension)
		{			
			case D3D11_SRV_DIMENSION_TEXTURE1D:
				MipLevel	= SRVDesc.Texture1D.MostDetailedMip;
				NumMips		= SRVDesc.Texture1D.MipLevels;
				break;
			case D3D11_SRV_DIMENSION_TEXTURE1DARRAY:
				MipLevel	= SRVDesc.Texture1DArray.MostDetailedMip;
				NumMips		= SRVDesc.Texture1DArray.MipLevels;
				ArraySlice	= SRVDesc.Texture1DArray.FirstArraySlice;
				NumSlices	= SRVDesc.Texture1DArray.ArraySize;
				break;
			case D3D11_SRV_DIMENSION_TEXTURE2D:
				MipLevel	= SRVDesc.Texture2D.MostDetailedMip;
				NumMips		= SRVDesc.Texture2D.MipLevels;
				break;
			case D3D11_SRV_DIMENSION_TEXTURE2DARRAY:
				MipLevel	= SRVDesc.Texture2DArray.MostDetailedMip;
				NumMips		= SRVDesc.Texture2DArray.MipLevels;
				ArraySlice	= SRVDesc.Texture2DArray.FirstArraySlice;
				NumSlices	= SRVDesc.Texture2DArray.ArraySize;
				break;
			case D3D11_SRV_DIMENSION_TEXTURE2DMS:
				MipLevel	= 0;
				NumMips		= 1;
				break;
			case D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY:
				MipLevel	= 0;
				NumMips		= 1;
				ArraySlice	= SRVDesc.Texture2DMSArray.FirstArraySlice;
				NumSlices	= SRVDesc.Texture2DMSArray.ArraySize;
				break;
			case D3D11_SRV_DIMENSION_TEXTURE3D:
				MipLevel	= SRVDesc.Texture3D.MostDetailedMip;
				NumMips		= SRVDesc.Texture3D.MipLevels;
				break;
			case D3D11_SRV_DIMENSION_TEXTURECUBE:
				MipLevel = SRVDesc.TextureCube.MostDetailedMip;
				NumMips		= SRVDesc.TextureCube.MipLevels;
				break;
			case D3D11_SRV_DIMENSION_TEXTURECUBEARRAY:
				MipLevel	= SRVDesc.TextureCubeArray.MostDetailedMip;
				NumMips		= SRVDesc.TextureCubeArray.MipLevels;
				ArraySlice	= SRVDesc.TextureCubeArray.First2DArrayFace;
				NumSlices	= SRVDesc.TextureCubeArray.NumCubes;
				break;
			case D3D11_SRV_DIMENSION_BUFFER:
			case D3D11_SRV_DIMENSION_BUFFEREX:
			default:
				break;
		}
	}
}

template <EShaderFrequency ShaderFrequency>
void FD3D11DynamicRHI::InternalSetShaderResourceView(FD3D11ViewableResource* Resource, ID3D11ShaderResourceView* SRV, int32 ResourceIndex)
{
	// Check either both are set, or both are null.
	check((Resource && SRV) || (!Resource && !SRV));

	//avoid state cache crash
	if (!((Resource && SRV) || (!Resource && !SRV)))
	{
		//UE_LOG(LogRHI, Warning, TEXT("Bailing on InternalSetShaderResourceView on resource: %i, %s"), ResourceIndex, *SRVName.ToString());
		return;
	}

	FD3D11ViewableResource*& ResourceSlot = CurrentResourcesBoundAsSRVs[ShaderFrequency][ResourceIndex];
	int32& MaxResourceIndex = MaxBoundShaderResourcesIndex[ShaderFrequency];

	if (Resource)
	{
		// We are binding a new SRV.
		// Update the max resource index to the highest bound resource index.
		MaxResourceIndex = FMath::Max(MaxResourceIndex, ResourceIndex);
		ResourceSlot = Resource;
	}
	else if (ResourceSlot != nullptr)
	{
		// Unbind the resource from the slot.
		ResourceSlot = nullptr;

		// If this was the highest bound resource...
		if (MaxResourceIndex == ResourceIndex)
		{
			// Adjust the max resource index downwards until we
			// hit the next non-null slot, or we've run out of slots.
			do
			{
				MaxResourceIndex--;
			}
			while (MaxResourceIndex >= 0 && CurrentResourcesBoundAsSRVs[ShaderFrequency][MaxResourceIndex] == nullptr);
		} 
	}

	// Set the SRV we have been given (or null).
	StateCache.SetShaderResourceView<ShaderFrequency>(SRV, ResourceIndex);
}

template void FD3D11DynamicRHI::InternalSetShaderResourceView<SF_Vertex>  (FD3D11ViewableResource* Resource, ID3D11ShaderResourceView* SRV, int32 ResourceIndex);
template void FD3D11DynamicRHI::InternalSetShaderResourceView<SF_Pixel>   (FD3D11ViewableResource* Resource, ID3D11ShaderResourceView* SRV, int32 ResourceIndex);
template void FD3D11DynamicRHI::InternalSetShaderResourceView<SF_Geometry>(FD3D11ViewableResource* Resource, ID3D11ShaderResourceView* SRV, int32 ResourceIndex);
template void FD3D11DynamicRHI::InternalSetShaderResourceView<SF_Compute> (FD3D11ViewableResource* Resource, ID3D11ShaderResourceView* SRV, int32 ResourceIndex);

void FD3D11DynamicRHI::TrackResourceBoundAsVB(FD3D11ViewableResource* Resource, int32 StreamIndex)
{
	check(StreamIndex >= 0 && StreamIndex < D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT);
	if (Resource)
	{
		// We are binding a new VB.
		// Update the max resource index to the highest bound resource index.
		MaxBoundVertexBufferIndex = FMath::Max(MaxBoundVertexBufferIndex, StreamIndex);
		CurrentResourcesBoundAsVBs[StreamIndex] = Resource;
	}
	else if (CurrentResourcesBoundAsVBs[StreamIndex] != nullptr)
	{
		// Unbind the resource from the slot.
		CurrentResourcesBoundAsVBs[StreamIndex] = nullptr;

		// If this was the highest bound resource...
		if (MaxBoundVertexBufferIndex == StreamIndex)
		{
			// Adjust the max resource index downwards until we
			// hit the next non-null slot, or we've run out of slots.
			do
			{
				MaxBoundVertexBufferIndex--;
			} while (MaxBoundVertexBufferIndex >= 0 && CurrentResourcesBoundAsVBs[MaxBoundVertexBufferIndex] == nullptr);
		}
	}
}

void FD3D11DynamicRHI::TrackResourceBoundAsIB(FD3D11ViewableResource* Resource)
{
	CurrentResourceBoundAsIB = Resource;
}

template <EShaderFrequency ShaderFrequency>
void FD3D11DynamicRHI::ClearShaderResourceViews(FD3D11ViewableResource* Resource)
{
	int32 MaxIndex = MaxBoundShaderResourcesIndex[ShaderFrequency];
	for (int32 ResourceIndex = MaxIndex; ResourceIndex >= 0; --ResourceIndex)
	{
		if (CurrentResourcesBoundAsSRVs[ShaderFrequency][ResourceIndex] == Resource)
		{
			// Unset the SRV from the device context
			SetShaderResourceView<ShaderFrequency>(nullptr, nullptr, ResourceIndex);
		}
	}
}

void FD3D11DynamicRHI::ConditionalClearShaderResource(FD3D11ViewableResource* Resource, bool bCheckBoundInputAssembler)
{
	SCOPE_CYCLE_COUNTER(STAT_D3D11ClearShaderResourceTime);
	check(Resource);
	ClearShaderResourceViews<SF_Vertex>(Resource);
	ClearShaderResourceViews<SF_Pixel>(Resource);
	ClearShaderResourceViews<SF_Geometry>(Resource);
	ClearShaderResourceViews<SF_Compute>(Resource);

	if (bCheckBoundInputAssembler)
	{
		for (int32 ResourceIndex = MaxBoundVertexBufferIndex; ResourceIndex >= 0; --ResourceIndex)
		{
			if (CurrentResourcesBoundAsVBs[ResourceIndex] == Resource)
			{
				// Unset the vertex buffer from the device context
				TrackResourceBoundAsVB(nullptr, ResourceIndex);
				StateCache.SetStreamSource(nullptr, ResourceIndex, 0);
			}
		}

		if (Resource == CurrentResourceBoundAsIB)
		{
			TrackResourceBoundAsIB(nullptr);
			StateCache.SetIndexBuffer(nullptr, DXGI_FORMAT_R16_UINT, 0);
		}
	}
}

template <EShaderFrequency ShaderFrequency>
void FD3D11DynamicRHI::ClearAllShaderResourcesForFrequency()
{
	int32 MaxIndex = MaxBoundShaderResourcesIndex[ShaderFrequency];
	for (int32 ResourceIndex = MaxIndex; ResourceIndex >= 0; --ResourceIndex)
	{
		if (CurrentResourcesBoundAsSRVs[ShaderFrequency][ResourceIndex] != nullptr)
		{
			// Unset the SRV from the device context
			SetShaderResourceView<ShaderFrequency>(nullptr, nullptr, ResourceIndex);
		}
	}
	StateCache.ClearConstantBuffers<ShaderFrequency>();
}

void FD3D11DynamicRHI::ClearAllShaderResources()
{
	ClearAllShaderResourcesForFrequency<SF_Vertex>();
	ClearAllShaderResourcesForFrequency<SF_Geometry>();
	ClearAllShaderResourcesForFrequency<SF_Pixel>();
	ClearAllShaderResourcesForFrequency<SF_Compute>();
}

#if (RHI_NEW_GPU_PROFILER == 0)

void FD3DGPUProfiler::BeginFrame(FD3D11DynamicRHI* InRHI)
{
	CurrentEventNode = NULL;
	check(!bTrackingEvents);
	check(!CurrentEventNodeFrame); // this should have already been cleaned up and the end of the previous frame
	
	// latch the bools from the game thread into our private copy
	bLatchedGProfilingGPU = GTriggerGPUProfile;
	bLatchedGProfilingGPUHitches = GTriggerGPUHitchProfile;
	if (bLatchedGProfilingGPUHitches)
	{
		bLatchedGProfilingGPU = false; // we do NOT permit an ordinary GPU profile during hitch profiles
	}

	// if we are starting a hitch profile or this frame is a gpu profile, then save off the state of the draw events
	if (bLatchedGProfilingGPU || (!bPreviousLatchedGProfilingGPUHitches && bLatchedGProfilingGPUHitches))
	{
		bOriginalGEmitDrawEvents = GetEmitDrawEvents();
	}

	if (bLatchedGProfilingGPU || bLatchedGProfilingGPUHitches)
	{
		if (bLatchedGProfilingGPUHitches && GPUHitchDebounce)
		{
			// if we are doing hitches and we had a recent hitch, wait to recover
			// the reasoning is that collecting the hitch report may itself hitch the GPU
			GPUHitchDebounce--; 
		}
		else
		{
			SetEmitDrawEvents(true);  // thwart an attempt to turn this off on the game side
			bTrackingEvents = true;
			CurrentEventNodeFrame = new FD3D11EventNodeFrame(InRHI);
			CurrentEventNodeFrame->StartFrame();
		}
	}
	else if (bPreviousLatchedGProfilingGPUHitches)
	{
		// hitch profiler is turning off, clear history and restore draw events
		GPUHitchEventNodeFrames.Empty();
		SetEmitDrawEvents(bOriginalGEmitDrawEvents);
	}
	bPreviousLatchedGProfilingGPUHitches = bLatchedGProfilingGPUHitches;

	FrameTiming.StartTiming();
}

void FD3DGPUProfiler::EndFrame()
{
	FrameTiming.EndTiming();

	if (FrameTiming.IsSupported())
	{
		uint64 GPUTiming = FrameTiming.GetTiming();
		uint64 GPUFreq = FrameTiming.GetTimingFrequency();
		GGPUFrameTime = FMath::TruncToInt( double(GPUTiming) / double(GPUFreq) / FPlatformTime::GetSecondsPerCycle() );
	}
	else
	{
		GGPUFrameTime = 0;
	}

	// if we have a frame open, close it now.
	if (CurrentEventNodeFrame)
	{
		CurrentEventNodeFrame->EndFrame();
	}

	check(!bTrackingEvents || bLatchedGProfilingGPU || bLatchedGProfilingGPUHitches);
	check(!bTrackingEvents || CurrentEventNodeFrame);
	if (bLatchedGProfilingGPU)
	{
		if (bTrackingEvents)
		{
			SetEmitDrawEvents(bOriginalGEmitDrawEvents);
			UE_LOG(LogD3D11RHI, Warning, TEXT(""));
			UE_LOG(LogD3D11RHI, Warning, TEXT(""));
			CurrentEventNodeFrame->DumpEventTree();
			GTriggerGPUProfile = false;
			bLatchedGProfilingGPU = false;

			if (RHIConfig::ShouldSaveScreenshotAfterProfilingGPU()
				&& GEngine->GameViewport)
			{
				GEngine->GameViewport->Exec( NULL, TEXT("SCREENSHOT"), *GLog);
			}
		}
	}
	else if (bLatchedGProfilingGPUHitches)
	{
		//@todo this really detects any hitch, even one on the game thread.
		// it would be nice to restrict the test to stalls on D3D, but for now...
		// this needs to be out here because bTrackingEvents is false during the hitch debounce
		static double LastTime = -1.0;
		double Now = FPlatformTime::Seconds();
		if (bTrackingEvents)
		{
			/** How long, in seconds a frame much be to be considered a hitch **/
			const float HitchThreshold = RHIConfig::GetGPUHitchThreshold();
			float ThisTime = Now - LastTime;
			bool bHitched = (ThisTime > HitchThreshold) && LastTime > 0.0 && CurrentEventNodeFrame;
			if (bHitched)
			{
				UE_LOG(LogD3D11RHI, Warning, TEXT("*******************************************************************************"));
				UE_LOG(LogD3D11RHI, Warning, TEXT("********** Hitch detected on CPU, frametime = %6.1fms"),ThisTime * 1000.0f);
				UE_LOG(LogD3D11RHI, Warning, TEXT("*******************************************************************************"));

				for (int32 Frame = 0; Frame < GPUHitchEventNodeFrames.Num(); Frame++)
				{
					UE_LOG(LogD3D11RHI, Warning, TEXT(""));
					UE_LOG(LogD3D11RHI, Warning, TEXT(""));
					UE_LOG(LogD3D11RHI, Warning, TEXT("********** GPU Frame: Current - %d"),GPUHitchEventNodeFrames.Num() - Frame);
					GPUHitchEventNodeFrames[Frame].DumpEventTree();
				}
				UE_LOG(LogD3D11RHI, Warning, TEXT(""));
				UE_LOG(LogD3D11RHI, Warning, TEXT(""));
				UE_LOG(LogD3D11RHI, Warning, TEXT("********** GPU Frame: Current"));
				CurrentEventNodeFrame->DumpEventTree();

				UE_LOG(LogD3D11RHI, Warning, TEXT("*******************************************************************************"));
				UE_LOG(LogD3D11RHI, Warning, TEXT("********** End Hitch GPU Profile"));
				UE_LOG(LogD3D11RHI, Warning, TEXT("*******************************************************************************"));
				if (GEngine->GameViewport)
				{
					GEngine->GameViewport->Exec( NULL, TEXT("SCREENSHOT"), *GLog);
				}

				GPUHitchDebounce = 5; // don't trigger this again for a while
				GPUHitchEventNodeFrames.Empty(); // clear history
			}
			else if (CurrentEventNodeFrame) // this will be null for discarded frames while recovering from a recent hitch
			{
				/** How many old frames to buffer for hitch reports **/
				static const int32 HitchHistorySize = 4;

				if (GPUHitchEventNodeFrames.Num() >= HitchHistorySize)
				{
					GPUHitchEventNodeFrames.RemoveAt(0);
				}
				GPUHitchEventNodeFrames.Add((FD3D11EventNodeFrame*)CurrentEventNodeFrame);
				CurrentEventNodeFrame = NULL;  // prevent deletion of this below; ke kept it in the history
			}
		}
		LastTime = Now;
	}
	bTrackingEvents = false;
	bTrackingGPUCrashData = false;
	delete CurrentEventNodeFrame;
	CurrentEventNodeFrame = NULL;
}

float FD3D11EventNode::GetTiming()
{
	float Result = 0;

	if (Timing.IsSupported())
	{
		// Get the timing result and block the CPU until it is ready
		const uint64 GPUTiming = Timing.GetTiming(true);
		const uint64 GPUFreq = Timing.GetTimingFrequency();

		Result = double(GPUTiming) / double(GPUFreq);
	}

	return Result;
}

FD3DGPUProfiler::FD3DGPUProfiler(class FD3D11DynamicRHI* InD3DRHI)
	: FGPUProfiler()
	, FrameTiming(InD3DRHI, 4)
	, D3D11RHI(InD3DRHI)
{
	// Initialize Buffered timestamp queries 
	FrameTiming.InitResource(FRHICommandListImmediate::Get());

	BeginFrame(InD3DRHI);
}

void FD3DGPUProfiler::PushEvent(const TCHAR* Name, FColor Color)
{
#if WITH_DX_PERF
	D3DPERF_BeginEvent(Color.DWColor(),Name);
#endif

	FGPUProfiler::PushEvent(Name, Color);
}

void FD3DGPUProfiler::PopEvent()
{
#if WITH_DX_PERF
	D3DPERF_EndEvent();
#endif

	FGPUProfiler::PopEvent();
}

/** Start this frame of per tracking */
void FD3D11EventNodeFrame::StartFrame()
{
	EventTree.Reset();
	DisjointQuery.StartTracking();
	RootEventTiming.StartTiming();
}

/** End this frame of per tracking, but do not block yet */
void FD3D11EventNodeFrame::EndFrame()
{
	RootEventTiming.EndTiming();
	DisjointQuery.EndTracking();
}

float FD3D11EventNodeFrame::GetRootTimingResults()
{
	double RootResult = 0.0f;
	if (RootEventTiming.IsSupported())
	{
		const uint64 GPUTiming = RootEventTiming.GetTiming(true);
		const uint64 GPUFreq = RootEventTiming.GetTimingFrequency();

		RootResult = double(GPUTiming) / double(GPUFreq);
	}

	return (float)RootResult;
}

void FD3D11EventNodeFrame::LogDisjointQuery()
{
	if (!DisjointQuery.IsResultValid())
	{
		UE_LOG(LogRHI, Warning, TEXT("%s"), TEXT("Profiled range was disjoint!  GPU switched to doing something else while profiling."));
	}
}

#endif // (RHI_NEW_GPU_PROFILER == 0)

static void D3D11UpdateBufferStatsCommon(ID3D11Buffer* Buffer, int64 BufferSize, bool bAllocating)
{
	// this is a work-around on Windows. Due to the fact that there is no way
	// to hook the actual d3d allocations we can't track the memory in the normal way.
	// Instead we simply tell LLM the size of these resources.

	LLM_SCOPED_PAUSE_TRACKING_WITH_ENUM_AND_AMOUNT(ELLMTag::GraphicsPlatform, bAllocating ? BufferSize : -BufferSize, ELLMTracker::Platform, ELLMAllocType::None);

#if UE_MEMORY_TRACE_ENABLED
	if (bAllocating)
	{
		MemoryTrace_Alloc((uint64)Buffer, BufferSize, 0, EMemoryTraceRootHeap::VideoMemory);
	}
	else
	{
		MemoryTrace_Free((uint64)Buffer, EMemoryTraceRootHeap::VideoMemory);
	}
#endif
}

void D3D11BufferStats::UpdateUniformBufferStats(ID3D11Buffer* Buffer, int64 BufferSize, bool bAllocating)
{
	UE::RHICore::UpdateGlobalUniformBufferStats(BufferSize, bAllocating);
	D3D11UpdateBufferStatsCommon(Buffer, BufferSize, bAllocating);
}

void D3D11BufferStats::UpdateBufferStats(FD3D11Buffer& Buffer, bool bAllocating)
{
	if (ID3D11Buffer* Resource = Buffer.Resource)
	{
		const FRHIBufferDesc& BufferDesc = Buffer.GetDesc();

		UE::RHICore::UpdateGlobalBufferStats(BufferDesc, BufferDesc.Size, bAllocating);
		D3D11UpdateBufferStatsCommon(Resource, BufferDesc.Size, bAllocating);
	}
}

void FD3D11DynamicRHI::UpdateMemoryStats()
{
#if PLATFORM_WINDOWS && (STATS || CSV_PROFILER_STATS)
	// Some older drivers don't support querying memory stats, so don't do anything if this fails.
	FD3DMemoryStats MemoryStats;
	if (SUCCEEDED(UE::DXGIUtilities::GetD3DMemoryStats(GetAdapter().DXGIAdapter, MemoryStats)))
	{
		UpdateD3DMemoryStatsAndCSV(MemoryStats, true);
	}
#endif // PLATFORM_WINDOWS && (STATS || CSV_PROFILER_STATS)
}

ID3D11Device* FD3D11DynamicRHI::RHIGetDevice() const
{
	return GetDevice();
}

ID3D11DeviceContext* FD3D11DynamicRHI::RHIGetDeviceContext() const
{
	return GetDeviceContext();
}

IDXGIAdapter* FD3D11DynamicRHI::RHIGetAdapter() const
{
	return GetAdapter().DXGIAdapter;
}

IDXGISwapChain* FD3D11DynamicRHI::RHIGetSwapChain(FRHIViewport* InViewport) const
{
	FD3D11Viewport* Viewport = static_cast<FD3D11Viewport*>(InViewport);
	return Viewport->GetSwapChain();
}

DXGI_FORMAT FD3D11DynamicRHI::RHIGetSwapChainFormat(EPixelFormat InFormat) const
{
	const DXGI_FORMAT PlatformFormat = UE::DXGIUtilities::FindDepthStencilFormat(static_cast<DXGI_FORMAT>(GPixelFormats[InFormat].PlatformFormat));
	return UE::DXGIUtilities::FindShaderResourceFormat(PlatformFormat, true);
}

ID3D11Buffer* FD3D11DynamicRHI::RHIGetResource(FRHIBuffer* InBuffer) const
{
	FD3D11Buffer* Buffer = ResourceCast(InBuffer);
	return Buffer->Resource;
}

ID3D11Resource* FD3D11DynamicRHI::RHIGetResource(FRHITexture* InTexture) const
{
	FD3D11Texture* D3D11Texture = ResourceCast(InTexture);
	return D3D11Texture->GetResource();
}

int64 FD3D11DynamicRHI::RHIGetResourceMemorySize(FRHITexture* InTexture) const
{
	FD3D11Texture* D3D11Texture = ResourceCast(InTexture);
	return D3D11Texture->GetMemorySize();
}

ID3D11RenderTargetView* FD3D11DynamicRHI::RHIGetRenderTargetView(FRHITexture* InTexture, int32 InMipIndex, int32 InArraySliceIndex) const
{
	FD3D11Texture* D3D11Texture = ResourceCast(InTexture);
	return D3D11Texture->GetRenderTargetView(InMipIndex, InArraySliceIndex);
}

ID3D11ShaderResourceView* FD3D11DynamicRHI::RHIGetShaderResourceView(FRHITexture* InTexture) const
{
	FD3D11Texture* D3D11Texture = ResourceCast(InTexture);
	return D3D11Texture->GetShaderResourceView();
}

void FD3D11DynamicRHI::RHIRegisterWork(uint32 NumPrimitives)
{
	RegisterGPUWork(NumPrimitives);
}

void FD3D11DynamicRHI::RHIVerifyResult(ID3D11Device* Device, HRESULT Result, const ANSICHAR* Code, const ANSICHAR* Filename, uint32 Line) const
{
	VerifyD3D11Result(Result, Code, Filename, Line, Device);
}

#if RHI_NEW_GPU_PROFILER
void FD3D11DynamicRHI::InsertProfilerTimestamp(uint64* Target)
{
	FD3D11RenderQuery* Query;
	if (Profiler.TimestampPool.IsEmpty())
	{
		Query = new FD3D11RenderQuery(FD3D11RenderQuery::EType::Profiler);
	}
	else
	{
		Query = Profiler.TimestampPool.Pop();
	}

	Query->End(Direct3DDeviceIMContext, Target);
}
#endif // RHI_NEW_GPU_PROFILER
