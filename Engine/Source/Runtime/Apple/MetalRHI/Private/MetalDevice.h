// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "MetalViewport.h"
#include "MetalCommandQueue.h"
#include "MetalBuffer.h"
#include "MetalCaptureManager.h"
#include "MetalTempAllocator.h"
#include "MetalStateCache.h"

#if PLATFORM_IOS
#include "IOS/IOSView.h"
#endif
#include "Containers/LockFreeList.h"

/**
 * Enumeration of features which are present only on some OS/device combinations.
 * These have to be checked at runtime as well as compile time to ensure backward compatibility.
 */
typedef NS_OPTIONS(uint64, EMetalFeatures)
{
	/** Support for specifying an update to the buffer offset only */
	EMetalFeaturesSetBufferOffset = 1 << 0,
	/** Supports NSUInteger counting visibility queries */
	EMetalFeaturesCountingQueries = 1 << 1,
	/** Supports base vertex/instance for draw calls */
	EMetalFeaturesBaseVertexInstance = 1 << 2,
	/** Supports indirect buffers for draw calls */
	EMetalFeaturesIndirectBuffer = 1 << 3,
	/** Supports layered rendering */
	EMetalFeaturesLayeredRendering = 1 << 4,
	/** Support for specifying small buffers as byte arrays */
	EMetalFeaturesSetBytes = 1 << 5,
	/** Unused Reserved Bit */
	EMetalFeaturesUnusedReservedBit6 = 1 << 6, // was EMetalFeaturesTessellation
	/** Supports framework-level validation */
	EMetalFeaturesValidation = 1 << 7,
	/** Supports detailed statistics */
	EMetalFeaturesStatistics = 1 << 8,
	/** Supports the explicit MTLHeap APIs */
	EMetalFeaturesHeaps = 1 << 9,
	/** Supports the explicit MTLFence APIs */
	EMetalFeaturesFences = 1 << 10,
	/** Supports MSAA Depth Resolves */
	EMetalFeaturesMSAADepthResolve = 1 << 11,
	/** Supports Store & Resolve in a single store action */
	EMetalFeaturesMSAAStoreAndResolve = 1 << 12,
	/** Supports framework GPU frame capture */
	EMetalFeaturesGPUTrace = 1 << 13,
	/** Supports the use of cubemap arrays */
	EMetalFeaturesCubemapArrays = 1 << 14,
	/** Supports the specification of multiple viewports and scissor rects */
	EMetalFeaturesMultipleViewports = 1 << 15,
	/** Supports minimum on-glass duration for drawables */
	EMetalFeaturesPresentMinDuration = 1llu << 16llu,
	/** Supports programmatic frame capture API */
	EMetalFeaturesGPUCaptureManager = 1llu << 17llu,
	/** Supports efficient buffer-blits */
	EMetalFeaturesEfficientBufferBlits = 1llu << 18llu,
	/** Supports any kind of buffer sub-allocation */
	EMetalFeaturesBufferSubAllocation = 1llu << 19llu,
	/** Supports private buffer sub-allocation */
	EMetalFeaturesPrivateBufferSubAllocation = 1llu << 20llu,
	/** Supports texture buffers */
	EMetalFeaturesTextureBuffers = 1llu << 21llu,
	/** Supports max. compute threads per threadgroup */
	EMetalFeaturesMaxThreadsPerThreadgroup = 1llu << 22llu,
	/** Supports parallel render encoders */
	EMetalFeaturesParallelRenderEncoders = 1llu << 23llu,
	/** Supports indirect argument buffers */
	EMetalFeaturesIABs = 1llu << 24llu,
	/** Supports specifying the mutability of buffers bound to PSOs */
	EMetalFeaturesPipelineBufferMutability = 1llu << 25llu,
	/** Supports tile shaders */
	EMetalFeaturesTileShaders = 1llu << 26llu,
	/** Unused Reserved Bit */
	EMetalFeaturesUnusedReservedBit27 = 1llu << 27llu, // was EMetalFeaturesSeparateTessellation
	/** Supports indirect argument buffers Tier 2 */
	EMetalFeaturesTier2IABs = 1llu << 28llu,
};

/**
 * EMetalDebugLevel: Level of Metal debug features to be enabled.
 */
enum EMetalDebugLevel
{
	EMetalDebugLevelOff,
	EMetalDebugLevelFastValidation,
	EMetalDebugLevelResetOnBind,
	EMetalDebugLevelConditionalSubmit,
	EMetalDebugLevelValidation,
	EMetalDebugLevelWaitForComplete,
};

class FMetalBindlessDescriptorManager;

class FMetalDevice
{
public:
	static FMetalDevice* CreateDevice();
	virtual ~FMetalDevice();
	
	void EnumerateFeatureSupport();
	inline bool SupportsFeature(EMetalFeatures InFeature) { return ((Features & InFeature) != 0); }
	
	inline FMetalResourceHeap& GetResourceHeap(void) { return Heap; }
	
	void EndDrawingViewport(bool bPresent);
	
	MTLTexturePtr CreateTexture(FMetalSurface* Surface, MTL::TextureDescriptor* Descriptor);
	FMetalBufferPtr CreatePooledBuffer(FMetalPooledBufferArgs const& Args);
	MTLEventPtr CreateEvent();
	
	void DrainHeap();
	void GarbageCollect();
	
	/** Get the index of the bound Metal device in the global list of rendering devices. */
	uint32 GetDeviceIndex(void) const;
	
	FMetalTempAllocator* GetTransferAllocator()
	{
		return TransferBufferAllocator;
	}
    
    FMetalTempAllocator* GetUniformAllocator()
    {
        return UniformBufferAllocator;
    }
    
    uint32 GetFrameNumberRHIThread()
    {
        return FrameNumberRHIThread;
    }
	
	FMetalCommandQueue& GetCommandQueue()
	{
		return *CommandQueue;
	}
	
#if PLATFORM_SUPPORTS_BINDLESS_RENDERING
    FMetalBindlessDescriptorManager* GetBindlessDescriptorManager()
    {
        return BindlessDescriptorManager;
    }
#endif
	
#if METAL_DEBUG_OPTIONS
    void AddActiveBuffer(MTL::Buffer* Buffer, const NS::Range& Range);
    void RemoveActiveBuffer(MTL::Buffer* Buffer, const NS::Range& Range);
	bool ValidateIsInactiveBuffer(MTL::Buffer* Buffer, const NS::Range& Range);
#endif
	
	MTL::Device* GetDevice()
	{
		return Device;
	}
	
	inline int32 GetRuntimeDebuggingLevel(void) const
	{
		return RuntimeDebuggingLevel;
	}
	
	void AddInflightCommandBuffer(MTL::CommandBuffer* CmdBuffer)
	{
		FScopeLock Lock(&InflightCommandBufferCriticalSection);
		InflightCommandBuffers.Add(CmdBuffer);
	}
	
	void RemoveInflightCommandBuffer(MTL::CommandBuffer* CmdBuffer)
	{
		FScopeLock Lock(&InflightCommandBufferCriticalSection);
		InflightCommandBuffers.Remove(CmdBuffer);
	}
	
	void WaitForGPUIdle()
	{
		TArray<MTL::CommandBuffer*> TempInflightCommandBuffers;
		{
			FScopeLock Lock(&InflightCommandBufferCriticalSection);
			TempInflightCommandBuffers = InflightCommandBuffers;
		}
		
		for(MTL::CommandBuffer* CmdBuffer : TempInflightCommandBuffers)
		{
			// Possible for the command buffer submission to be in progress, yield until committed
			while(CmdBuffer->status() < MTL::CommandBufferStatusCommitted)
			{
				FPlatformProcess::Yield();
			}
			
			CmdBuffer->waitUntilCompleted();
		}
	}
	
	void IncrementFrameRHIThread()
	{
		FrameNumberRHIThread++;
	}
	
	dispatch_semaphore_t& GetFrameSemaphore()
	{
		return FrameSemaphore;
	}
	
private:
	FMetalDevice(MTL::Device* MetalDevice, uint32 DeviceIndex);
	
	void FlushFreeList(bool const bFlushFences = true);
	
private:
	MTL::Device* Device;
	
	FMetalCommandQueue* CommandQueue;
	
	/** A sempahore used to ensure that wait for previous frames to complete if more are in flight than we permit */
	dispatch_semaphore_t FrameSemaphore;
	
	/** The index into the GPU device list for the selected Metal device */
	uint32 DeviceIndex;
	
	/** Dynamic memory heap */
	FMetalResourceHeap Heap;
	
	/** GPU Frame Capture Manager */
	FMetalCaptureManager* CaptureManager;
	
    FMetalTempAllocator* UniformBufferAllocator;
	FMetalTempAllocator* TransferBufferAllocator;
	
#if METAL_DEBUG_OPTIONS
	/** The list of fences for the current frame */
	TArray<FMetalFence*> FrameFences;
    
    FCriticalSection ActiveBuffersMutex;
    
    /** These are the active buffers that cannot be CPU modified */
    TMap<MTL::Buffer*, TArray<NS::Range>> ActiveBuffers;
#endif
	
	/** Critical section for FreeList */
	FCriticalSection FreeListMutex;
	
	/** Event for coordinating pausing of render thread to keep inline with the ios display link. */
	FEvent* FrameReadyEvent;
	
	/** Internal frame counter, used to ensure that we only drain the buffer pool one after each frame within RHIEndFrame. */
	uint32 FrameCounter = 0;
	
	/** Bitfield of supported Metal features with varying availability depending on OS/device */
	uint64 Features = 0;
	
	/** PSO cache manager */
	FMetalPipelineStateCacheManager* PSOManager;

    /** Thread index owned by the RHI Thread. Monotonically increases every call to EndFrame() */
    uint32 FrameNumberRHIThread = 0;

	int32 RuntimeDebuggingLevel = 0;

	FCriticalSection 			   InflightCommandBufferCriticalSection;
	TArray<MTL::CommandBuffer*>	   InflightCommandBuffers;
	
#if PLATFORM_SUPPORTS_BINDLESS_RENDERING
    /** Bindless Descriptor Heaps manager. */
    FMetalBindlessDescriptorManager* BindlessDescriptorManager;
#endif

#if METAL_RHI_RAYTRACING
	FMetalRayTracingCompactionRequestHandler* RayTracingCompactionRequestHandler;

	void InitializeRayTracing();
	void CleanUpRayTracing();

public:
	void UpdateRayTracing();

	inline FMetalRayTracingCompactionRequestHandler* GetRayTracingCompactionRequestHandler() const { return RayTracingCompactionRequestHandler; }
#endif // METAL_RHI_RAYTRACING
};

