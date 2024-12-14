// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MetalRHIRenderQuery.cpp: Metal RHI Render Query Implementation.
=============================================================================*/

#include "MetalRHIRenderQuery.h"
#include "MetalDevice.h"
#include "MetalRHIPrivate.h"
#include "MetalProfiler.h"
#include "MetalLLM.h"
#include "MetalCommandBuffer.h"
#include "MetalRHIContext.h"
#include "HAL/PThreadEvent.h"
#include "RenderCore.h"

//------------------------------------------------------------------------------

#pragma mark - Metal RHI Private Query Buffer Resource Class -


FMetalQueryBuffer::FMetalQueryBuffer(FMetalQueryBufferPool* InPool, FMetalBufferPtr InBuffer)
	: FRHIResource(RRT_TimestampCalibrationQuery)
	, Pool(InPool)
	, Buffer(InBuffer)
	, WriteOffset(0)
{
	// void
}

FMetalQueryBuffer::~FMetalQueryBuffer()
{
	if (GIsMetalInitialized)
	{
		if (Buffer)
		{
			if (Pool)
			{
				Pool->ReleaseQueryBuffer(Buffer);
			}
		}
	}
}

uint64 FMetalQueryBuffer::GetResult(uint32 Offset)
{
	uint64 Result = 0;
    MTL_SCOPED_AUTORELEASE_POOL;
    {
		Result = *((uint64 const*)(((uint8*)Buffer->Contents()) + Offset));
	}
	return Result;
}


//------------------------------------------------------------------------------

#pragma mark - Metal RHI Private Query Buffer Pool Class -


FMetalQueryBufferPool::FMetalQueryBufferPool(FMetalDevice& InDevice)
	: CurrentBuffer{nullptr}
	, Buffers{}
	, Device{InDevice}
{
	// void
}

FMetalQueryBufferPool::~FMetalQueryBufferPool()
{
	// void
}

void FMetalQueryBufferPool::Allocate(FMetalQueryResult& NewQuery)
{
	FMetalQueryBuffer* QB = IsValidRef(CurrentBuffer) ? CurrentBuffer.GetReference() : GetCurrentQueryBuffer();

	uint32 Offset = Align(QB->WriteOffset, EQueryBufferAlignment);
	uint32 End = Align(Offset + EQueryResultMaxSize, EQueryBufferAlignment);

	if (Align(QB->WriteOffset, EQueryBufferAlignment) + EQueryResultMaxSize <= EQueryBufferMaxSize)
	{
		NewQuery.SourceBuffer = QB;
		NewQuery.Offset = Align(QB->WriteOffset, EQueryBufferAlignment);
		QB->WriteOffset = Align(QB->WriteOffset, EQueryBufferAlignment) + EQueryResultMaxSize;
	}
	else
	{
		UE_LOG(LogMetal, Fatal, TEXT("No memory left in pool, check EQueryBufferMaxSize"));
	}
}

FMetalQueryBuffer* FMetalQueryBufferPool::GetCurrentQueryBuffer()
{
	if (!IsValidRef(CurrentBuffer) || (CurrentBuffer->Buffer->GetMTLBuffer()->storageMode() != MTL::StorageModeShared && CurrentBuffer->WriteOffset > 0))
	{
		FMetalBufferPtr Buffer;
		if (Buffers.Num())
		{
			Buffer = Buffers.Pop();
		}
		else
		{
			LLM_SCOPE_METAL(ELLMTagMetal::Buffers);
			LLM_PLATFORM_SCOPE_METAL(ELLMTagMetal::Buffers);

			METAL_GPUPROFILE(FScopedMetalCPUStats CPUStat(FString::Printf(TEXT("AllocBuffer: %llu, %llu"), EQueryBufferMaxSize, MTL::ResourceStorageModeShared)));
			
			MTL::ResourceOptions HazardTrackingMode = MTL::ResourceHazardTrackingModeUntracked;
			static bool bSupportsHeaps = Device.SupportsFeature(EMetalFeaturesHeaps);
			if(bSupportsHeaps)
			{
				HazardTrackingMode = MTL::ResourceHazardTrackingModeTracked;
			}
			
			Buffer = Device.GetResourceHeap().CreateBuffer(EQueryBufferMaxSize, 16, BUF_Dynamic, FMetalCommandQueue::GetCompatibleResourceOptions((MTL::ResourceOptions)(BUFFER_CACHE_MODE | HazardTrackingMode | MTL::ResourceStorageModeShared)), true);
			FMemory::Memzero((((uint8*)Buffer->Contents())), EQueryBufferMaxSize);

#if STATS || ENABLE_LOW_LEVEL_MEM_TRACKER
			MetalLLM::LogAllocBuffer(Buffer);
#endif // STATS || ENABLE_LOW_LEVEL_MEM_TRACKER
		}

		CurrentBuffer = new FMetalQueryBuffer(this, MoveTemp(Buffer));
	}

	return CurrentBuffer.GetReference();
}

void FMetalQueryBufferPool::ReleaseCurrentQueryBuffer()
{
	if (IsValidRef(CurrentBuffer) && CurrentBuffer->WriteOffset > 0)
	{
		CurrentBuffer.SafeRelease();
	}
}

void FMetalQueryBufferPool::ReleaseQueryBuffer(FMetalBufferPtr Buffer)
{
	Buffers.Add(Buffer);
}


//------------------------------------------------------------------------------

#pragma mark - Metal RHI Private Query Result Class -

void FMetalQueryResult::Reset()
{
	CommandBufferFence.Reset();
	bCompleted = false;
}

bool FMetalQueryResult::Wait(uint64 Millis)
{
	if (!bCompleted)
	{
		bCompleted = CommandBufferFence->Wait(Millis);
		return bCompleted;
	}
	return true;
}

uint64 FMetalQueryResult::GetResult()
{
	if (IsValidRef(SourceBuffer))
	{
		return SourceBuffer->GetResult(Offset);
	}
	return 0;
}


//------------------------------------------------------------------------------

#pragma mark - Metal RHI Render Query Class -


FMetalRHIRenderQuery::FMetalRHIRenderQuery(FMetalDevice& MetalDevice, ERenderQueryType InQueryType)
	: Device(MetalDevice)
	, Type{InQueryType}
	, Buffer{}
	, Result{0}
	, bAvailable{false}
	, QueryWrittenEvent(nullptr)
{
	// void
}

FMetalRHIRenderQuery::~FMetalRHIRenderQuery()
{
	Buffer.SourceBuffer.SafeRelease();
	Buffer.Offset = 0;
	
	if(QueryWrittenEvent != nullptr)
	{
		FPlatformProcess::ReturnSynchEventToPool(QueryWrittenEvent);
		QueryWrittenEvent = nullptr;
	}
}

void FMetalRHIRenderQuery::Begin_TopOfPipe()
{
	Buffer.Reset();
	bAvailable = false;
}

void FMetalRHIRenderQuery::End_TopOfPipe()
{
	if (Type == RQT_AbsoluteTime)
	{
		Buffer.Reset();
		if(QueryWrittenEvent)
		{
			QueryWrittenEvent->Reset();
		}
	}
	bAvailable = false;
}

void FMetalRHIRenderQuery::Begin(FMetalRHICommandContext* Context, TSharedPtr<FMetalCommandBufferFence, ESPMode::ThreadSafe> const& BatchFence)
{
	Buffer.CommandBufferFence.Reset();
	Buffer.SourceBuffer.SafeRelease();
	Buffer.Offset = 0;
	Buffer.bBatchFence = false;

	Result = 0;
	bAvailable = false;

	switch (Type)
	{
		case RQT_Occlusion:
		{
			// allocate our space in the current buffer
			Context->GetQueryBufferPool()->Allocate(Buffer);
			Buffer.bCompleted = false;

			if ((GMaxRHIFeatureLevel >= ERHIFeatureLevel::SM5) && Device.SupportsFeature(EMetalFeaturesCountingQueries))
			{
				Context->GetStateCache().SetVisibilityResultMode(MTL::VisibilityResultModeCounting, Buffer.Offset);
			}
			else
			{
				Context->GetStateCache().SetVisibilityResultMode(MTL::VisibilityResultModeBoolean, Buffer.Offset);
			}
			if (BatchFence.IsValid())
			{
				Buffer.CommandBufferFence = BatchFence;
				Buffer.bBatchFence = true;
			}
			else
			{
				Buffer.CommandBufferFence = MakeShareable(new FMetalCommandBufferFence);
			}
			break;
		}
		case RQT_AbsoluteTime:
		{
			break;
		}
		default:
		{
			check(0);
			break;
		}
	}
}

void FMetalRHIRenderQuery::End(FMetalRHICommandContext* Context)
{
	switch (Type)
	{
		case RQT_Occlusion:
		{
			// switch back to non-occlusion rendering
			check(Buffer.CommandBufferFence.IsValid());
			Context->GetStateCache().SetVisibilityResultMode(MTL::VisibilityResultModeDisabled, 0);

			// For unique, unbatched, queries insert the fence now
			if (!Buffer.bBatchFence)
			{
				Context->InsertCommandBufferFence(Buffer.CommandBufferFence, FMetalCommandBufferCompletionHandler());
			}
			break;
		}
		case RQT_AbsoluteTime:
		{
			AddRef();

			// Reset the result availability state
			Buffer.SourceBuffer.SafeRelease();
			Buffer.Offset = 0;
			Buffer.bCompleted = false;
			Buffer.bBatchFence = false;
			TSharedPtr<FMetalCommandBufferFence, ESPMode::ThreadSafe> CommandBufferFence = MakeShareable(new FMetalCommandBufferFence);

			Result = 0;
			bAvailable = false;

			if(QueryWrittenEvent == nullptr)
			{
				QueryWrittenEvent = FPlatformProcess::GetSynchEventFromPool(true);
				check(QueryWrittenEvent != nullptr);
			}
			else
			{
				QueryWrittenEvent->Reset();
			}

			// Insert the fence to wait on the current command buffer
            FMetalCommandBufferCompletionHandler Handler;
            Handler.BindLambda([this](MTL::CommandBuffer* CmdBuffer)
			{
				Result = 0;
				
                // If there are no commands in the command buffer then this can be zero
                // In this case GPU start time is also not correct - we need to fall back standard behaviour
                // Only seen empty command buffers at the very end of a frame
                if (IsRHIDeviceApple())
                {
                    Result = uint64(CmdBuffer->GPUEndTime() - CmdBuffer->GPUStartTime()) * 1000;
                }
                else
                {
                    Result = uint64((CmdBuffer->GPUEndTime() / 1000.0) / FPlatformTime::GetSecondsPerCycle64());
                }
				
				if(Result == 0)
				{
					Result = (FPlatformTime::ToMilliseconds64(mach_absolute_time()) * 1000.0);
				}

				if(QueryWrittenEvent != nullptr)
				{
					QueryWrittenEvent->Trigger();
				}
				
				this->Release();
			});

			Context->InsertCommandBufferFence(CommandBufferFence, Handler);
			Buffer.CommandBufferFence = CommandBufferFence;
			
			Context->SplitCommandBuffers();
            
			break;
		}
		default:
		{
			check(0);
			break;
		}
	}
}

bool FMetalRHIRenderQuery::GetResult(uint64& OutNumPixels, bool bWait, uint32 GPUIndex)
{
	if (!bAvailable)
	{
		SCOPE_CYCLE_COUNTER(STAT_RenderQueryResultTime);

		bool bOK = false;

		bool const bCmdBufferIncomplete = !Buffer.bCompleted;

		// timer queries are used for Benchmarks which can stall a bit more
		uint64 WaitMS = (Type == RQT_AbsoluteTime) ? 30000 : 500;
		if (bWait)
		{
			FRHICommandListImmediate& RHICmdList = FRHICommandListImmediate::Get();
			
			// RHI thread *must* be flushed at this point if the internal handles we rely upon are not yet valid.
			if (!Buffer.CommandBufferFence.IsValid())
			{
				RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);
			}

			{
				FRenderThreadIdleScope IdleScope(ERenderThreadIdleTypes::WaitingForGPUQuery);
				bOK = Buffer.Wait(WaitMS);
			}

			// Result is written in one of potentially many command buffer completion handlers
			// But the command buffer wait above may return before the query completion handler fires
			// We need to wait here until that has happenned, also make sure the command buffer actually completed due to timeout
			if (bOK && (Type == RQT_AbsoluteTime) && QueryWrittenEvent != nullptr)
			{
				QueryWrittenEvent->Wait();
			}

			// Never wait for a failed signal again.
			bAvailable = Buffer.bCompleted;
		}
		else
		{
			if(Buffer.CommandBufferFence.IsValid())
			{
				bOK = Buffer.Wait(0);
			}
		}

		if (bOK == false)
		{
			OutNumPixels = 0;
			UE_CLOG(bWait, LogMetal, Display, TEXT("Timed out while waiting for GPU to catch up. (%llu ms)"), WaitMS);
			return false;
		}

		if (Type == RQT_Occlusion)
		{
			Result = Buffer.GetResult();
		}

		Buffer.SourceBuffer.SafeRelease();
	}

	// at this point, we are ready to read the value!
	OutNumPixels = Result;

	return true;
}
