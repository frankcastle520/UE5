// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetalDevice.h"
#include "MetalRHI.h"
#include "MetalRHIPrivate.h"
#include "MetalRHIRenderQuery.h"
#include "MetalVertexDeclaration.h"
#include "MetalShaderTypes.h"
#include "MetalGraphicsPipelineState.h"
#include "MetalCommandEncoder.h"
#include "MetalRHIContext.h"
#include "Misc/App.h"
#if PLATFORM_IOS
#include "IOS/IOSAppDelegate.h"
#endif
#include "Misc/ConfigCacheIni.h"
#include "HAL/PlatformFramePacer.h"
#include "Runtime/HeadMountedDisplay/Public/IHeadMountedDisplayModule.h"

#include "MetalProfiler.h"
#include "MetalCommandBuffer.h"

#include "MetalBindlessDescriptors.h"
#include "MetalTempAllocator.h"

int32 GMetalSupportsIntermediateBackBuffer = 0;
static FAutoConsoleVariableRef CVarMetalSupportsIntermediateBackBuffer(
	TEXT("rhi.Metal.SupportsIntermediateBackBuffer"),
	GMetalSupportsIntermediateBackBuffer,
	TEXT("When enabled (> 0) allocate an intermediate texture to use as the back-buffer & blit from there into the actual device back-buffer, this is required if we use the experimental separate presentation thread. (Off by default (0))"), ECVF_ReadOnly);

int32 GMetalSeparatePresentThread = 0;
static FAutoConsoleVariableRef CVarMetalSeparatePresentThread(
	TEXT("rhi.Metal.SeparatePresentThread"),
	GMetalSeparatePresentThread,
	TEXT("When enabled (> 0) requires rhi.Metal.SupportsIntermediateBackBuffer be enabled and will cause two intermediate back-buffers be allocated so that the presentation of frames to the screen can be run on a separate thread.\n")
	TEXT("This option uncouples the Render/RHI thread from calls to -[CAMetalLayer nextDrawable] and will run arbitrarily fast by rendering but not waiting to present all frames. This is equivalent to running without V-Sync, but without the screen tearing.\n")
	TEXT("On iOS/tvOS this is the only way to run without locking the CPU to V-Sync somewhere - this shouldn't be used in a shipping title without understanding the power/heat implications.\n")
	TEXT("(Off by default (0))"), ECVF_ReadOnly);

#if PLATFORM_MAC
static int32 GMetalCommandQueueSize = 5120; // This number is large due to texture streaming - currently each texture is its own command-buffer.
// The whole MetalRHI needs to be changed to use MTLHeaps/MTLFences & reworked so that operations with the same synchronisation requirements are collapsed into a single blit command-encoder/buffer.
#else
static int32 GMetalCommandQueueSize = 0;
#endif

#if METAL_DEBUG_OPTIONS
int32 GMetalBufferScribble = 0; // Deliberately not static, see InitFrame_UniformBufferPoolCleanup
static FAutoConsoleVariableRef CVarMetalBufferScribble(
	TEXT("rhi.Metal.BufferScribble"),
	GMetalBufferScribble,
	TEXT("Debug option: when enabled will scribble over the buffer contents with a single value when releasing buffer objects, or regions thereof. (Default: 0, Off)"));

int32 GMetalResourcePurgeOnDelete = 0;
static FAutoConsoleVariableRef CVarMetalResourcePurgeOnDelete(
	TEXT("rhi.Metal.ResourcePurgeOnDelete"),
	GMetalResourcePurgeOnDelete,
	TEXT("Debug option: when enabled all MTLResource objects will have their backing stores purged on release - any subsequent access will be invalid and cause a command-buffer failure. Useful for making intermittent resource lifetime errors more common and easier to track. (Default: 0, Off)"));

static int32 GMetalResourceDeferDeleteNumFrames = 0;
static FAutoConsoleVariableRef CVarMetalResourceDeferDeleteNumFrames(
	TEXT("rhi.Metal.ResourceDeferDeleteNumFrames"),
	GMetalResourceDeferDeleteNumFrames,
	TEXT("Debug option: set to the number of frames that must have passed before resource free-lists are processed and resources disposed of. (Default: 0, Off)"));
#endif

#if UE_BUILD_SHIPPING
int32 GMetalRuntimeDebugLevel = 0;
#else
int32 GMetalRuntimeDebugLevel = 1;
#endif
static FAutoConsoleVariableRef CVarMetalRuntimeDebugLevel(
	TEXT("rhi.Metal.RuntimeDebugLevel"),
	GMetalRuntimeDebugLevel,
	TEXT("The level of debug validation performed by MetalRHI in addition to the underlying Metal API & validation layer.\n")
	TEXT("Each subsequent level adds more tests and reporting in addition to the previous level.\n")
	TEXT("*LEVELS >= 3 ARE IGNORED IN SHIPPING AND TEST BUILDS*. (Default: 1 (Debug, Development), 0 (Test, Shipping))\n")
	TEXT("\t0: Off,\n")
	TEXT("\t1: Enable light-weight validation of resource bindings & API usage,\n")
	TEXT("\t2: Reset resource bindings when binding a PSO/Compute-Shader to simplify GPU debugging,\n")
	TEXT("\t3: Allow rhi.Metal.CommandBufferCommitThreshold to break command-encoders (except when MSAA is enabled),\n")
	TEXT("\t4: Enable slower, more extensive validation checks for resource types & encoder usage,\n")
    TEXT("\t5: Wait for each command-buffer to complete immediately after submission."));

float GMetalPresentFramePacing = 0.0f;
#if !PLATFORM_MAC
static FAutoConsoleVariableRef CVarMetalPresentFramePacing(
	TEXT("rhi.Metal.PresentFramePacing"),
	GMetalPresentFramePacing,
	TEXT("Specify the desired frame rate for presentation (iOS 10.3+ only, default: 0.0f, off"));
#endif

#if PLATFORM_MAC
static int32 GMetalDefaultUniformBufferAllocation = 1024 * 1024 * 2;
#else
static int32 GMetalDefaultUniformBufferAllocation = 1024 * 256;
#endif
static FAutoConsoleVariableRef CVarMetalDefaultUniformBufferAllocation(
    TEXT("rhi.Metal.DefaultUniformBufferAllocation"),
    GMetalDefaultUniformBufferAllocation,
    TEXT("Default size of a uniform buffer allocation."));

#if PLATFORM_MAC
static int32 GMetalTargetUniformAllocationLimit = 1024 * 1024 * 50;
#else
static int32 GMetalTargetUniformAllocationLimit = 1024 * 1024 * 5;
#endif
static FAutoConsoleVariableRef CVarMetalTargetUniformAllocationLimit(
     TEXT("rhi.Metal.TargetUniformAllocationLimit"),
     GMetalTargetUniformAllocationLimit,
     TEXT("Target Allocation limit for the uniform buffer pool."));

#if PLATFORM_MAC
static int32 GMetalTargetTransferAllocatorLimit = 1024*1024*50;
#else
static int32 GMetalTargetTransferAllocatorLimit = 1024*1024*2;
#endif
static FAutoConsoleVariableRef CVarMetalTargetTransferAllocationLimit(
	TEXT("rhi.Metal.TargetTransferAllocationLimit"),
	GMetalTargetTransferAllocatorLimit,
	TEXT("Target Allocation limit for the upload staging buffer pool."));

#if PLATFORM_MAC
static int32 GMetalDefaultTransferAllocation = 1024*1024*10;
#else
static int32 GMetalDefaultTransferAllocation = 1024*1024*1;
#endif
static FAutoConsoleVariableRef CVarMetalDefaultTransferAllocation(
	TEXT("rhi.Metal.DefaultTransferAllocation"),
	GMetalDefaultTransferAllocation,
	TEXT("Default size of a single entry in the upload pool."));

static int32 GForceNoMetalFence = 1;
static FAutoConsoleVariableRef CVarMetalForceNoFence(
	TEXT("rhi.Metal.ForceNoFence"),
	GForceNoMetalFence,
	TEXT("[IOS] When enabled, act as if -nometalfence was on the commandline\n")
	TEXT("(On by default (1))"));

static int32 GForceNoMetalHeap = 1;
static FAutoConsoleVariableRef CVarMetalForceNoHeap(
	TEXT("rhi.Metal.ForceNoHeap"),
	GForceNoMetalHeap,
	TEXT("[IOS] When enabled, act as if -nometalheap was on the commandline\n")
	TEXT("(On by default (1))"));

#if PLATFORM_MAC
static NS::Object* GMetalDeviceObserver;
static MTL::Device* GetMTLDevice(uint32& DeviceIndex)
{
#if PLATFORM_MAC_ARM64
    return MTL::CreateSystemDefaultDevice();
#else
    MTL_SCOPED_AUTORELEASE_POOL;
	
	DeviceIndex = 0;
	
	NS::Array* DeviceList;
	
    DeviceList = MTL::CopyAllDevicesWithObserver(&GMetalDeviceObserver, [](const MTL::Device* Device, const NS::String* Notification)
    {
        if (Notification->isEqualToString(MTL::DeviceWasAddedNotification))
        {
            FPlatformMisc::GPUChangeNotification(Device->registryID(), FPlatformMisc::EMacGPUNotification::Added);
        }
        else if (Notification->isEqualToString(MTL::DeviceRemovalRequestedNotification))
        {
            FPlatformMisc::GPUChangeNotification(Device->registryID(), FPlatformMisc::EMacGPUNotification::RemovalRequested);
        }
        else if (Notification->isEqualToString(MTL::DeviceWasRemovedNotification))
        {
            FPlatformMisc::GPUChangeNotification(Device->registryID(), FPlatformMisc::EMacGPUNotification::Removed);
        }
    });
	
	const int32 NumDevices = DeviceList->count();
	
	TArray<FMacPlatformMisc::FGPUDescriptor> const& GPUs = FPlatformMisc::GetGPUDescriptors();
	check(GPUs.Num() > 0);

	// @TODO  here, GetGraphicsAdapterLuid() is used as a device index (how the function "GetGraphicsAdapter" used to work)
	//        eventually we want the HMD module to return the MTLDevice's registryID, but we cannot fully handle that until
	//        we drop support for 10.12
	//  NOTE: this means any implementation of GetGraphicsAdapterLuid() for Mac should return an index, and use -1 as a 
	//        sentinel value representing "no device" (instead of 0, which is used in the LUID case)
	int32 HmdGraphicsAdapter  = IHeadMountedDisplayModule::IsAvailable() ? (int32)IHeadMountedDisplayModule::Get().GetGraphicsAdapterLuid() : -1;
 	int32 OverrideRendererId = FPlatformMisc::GetExplicitRendererIndex();
	
	int32 ExplicitRendererId = OverrideRendererId >= 0 ? OverrideRendererId : HmdGraphicsAdapter;
	if(ExplicitRendererId < 0 && GPUs.Num() > 1)
	{
		OverrideRendererId = -1;
		bool bForceExplicitRendererId = false;
		for(uint32 i = 0; i < GPUs.Num(); i++)
		{
			FMacPlatformMisc::FGPUDescriptor const& GPU = GPUs[i];
			if(!GPU.GPUHeadless && GPU.GPUVendorId != (uint32)EGpuVendorId::Intel)
			{
				OverrideRendererId = i;
			}
		}
		if (bForceExplicitRendererId)
		{
			ExplicitRendererId = OverrideRendererId;
		}
	}
	
	MTL::Device* SelectedDevice = nullptr;
	if (ExplicitRendererId >= 0 && ExplicitRendererId < GPUs.Num())
	{
		FMacPlatformMisc::FGPUDescriptor const& GPU = GPUs[ExplicitRendererId];
		TArray<FString> NameComponents;
		FString(GPU.GPUName).TrimStart().ParseIntoArray(NameComponents, TEXT(" "));	
		for (uint32 index = 0; index < NumDevices; index++)
		{
			MTL::Device* Device = (MTL::Device*)DeviceList->object(index);
			
            FString DeviceName = NSStringToFString(Device->name());
            
            if((Device->registryID() == GPU.RegistryID))
            {
                DeviceIndex = ExplicitRendererId;
                SelectedDevice = Device;
            }
			else if((DeviceName.Find(TEXT("AMD"), ESearchCase::IgnoreCase) != -1 && GPU.GPUVendorId == (uint32)EGpuVendorId::Amd)
			   || (DeviceName.Find(TEXT("Intel"), ESearchCase::IgnoreCase) != -1 && GPU.GPUVendorId == (uint32)EGpuVendorId::Intel))
			{
				bool bMatchesName = (NameComponents.Num() > 0);
				for (FString& Component : NameComponents)
				{
					bMatchesName &= DeviceName.Contains(Component);
				}
				if((Device->isHeadless() == GPU.GPUHeadless || GPU.GPUVendorId != (uint32)EGpuVendorId::Amd) && bMatchesName)
                {
					DeviceIndex = ExplicitRendererId;
					SelectedDevice = Device;
					break;
				}
			}
		}
		if(!SelectedDevice)
		{
			UE_LOG(LogMetal, Warning,  TEXT("Couldn't find Metal device to match GPU descriptor (%s) from IORegistry - using default device."), *FString(GPU.GPUName));
		}
	}
	if (SelectedDevice == nullptr)
	{
		TArray<FString> NameComponents;
		SelectedDevice = MTL::CreateSystemDefaultDevice();
		bool bFoundDefault = false;
		for (uint32 i = 0; i < GPUs.Num(); i++)
		{
			FMacPlatformMisc::FGPUDescriptor const& GPU = GPUs[i];
            FString DeviceName = NSStringToFString(SelectedDevice->name());

            if((SelectedDevice->registryID() == GPU.RegistryID))
            {
                DeviceIndex = i;
                bFoundDefault = true;
                break;
            }
            else if((DeviceName.Find(TEXT("AMD"), ESearchCase::IgnoreCase) != -1 && GPU.GPUVendorId == (uint32)EGpuVendorId::Amd)
                   || (DeviceName.Find(TEXT("Intel"), ESearchCase::IgnoreCase) != -1 && GPU.GPUVendorId == (uint32)EGpuVendorId::Intel))
			{
				NameComponents.Empty();
				bool bMatchesName = FString(GPU.GPUName).TrimStart().ParseIntoArray(NameComponents, TEXT(" ")) > 0;
				for (FString& Component : NameComponents)
				{
					bMatchesName &= DeviceName.Contains(Component);
				}
				if((SelectedDevice->isHeadless() == GPU.GPUHeadless || GPU.GPUVendorId != (uint32)EGpuVendorId::Amd) && bMatchesName)
                {
					DeviceIndex = i;
					bFoundDefault = true;
					break;
				}
			}
		}
		if(!bFoundDefault)
		{
			UE_LOG(LogMetal, Warning,  TEXT("Couldn't find Metal device %s in GPU descriptors from IORegistry - capability reporting may be wrong."), *NSStringToFString(SelectedDevice->name()));
		}
	}
	return SelectedDevice;
#endif // PLATFORM_MAC_ARM64
}

MTL::PrimitiveTopologyClass TranslatePrimitiveTopology(uint32 PrimitiveType)
{
	switch (PrimitiveType)
	{
		case PT_TriangleList:
		case PT_TriangleStrip:
			return MTL::PrimitiveTopologyClassTriangle;
		case PT_LineList:
			return MTL::PrimitiveTopologyClassLine;
		case PT_PointList:
			return MTL::PrimitiveTopologyClassPoint;
		default:
			UE_LOG(LogMetal, Fatal, TEXT("Unsupported primitive topology %d"), (int32)PrimitiveType);
			return MTL::PrimitiveTopologyClassTriangle;
	}
}
#endif

FMetalDevice* FMetalDevice::CreateDevice()
{
	uint32 DeviceIndex = 0;
#if PLATFORM_VISIONOS && UE_USE_SWIFT_UI_MAIN
	// get the device from the compositor layer
	MTL::Device* Device = (__bridge MTL::Device*)cp_layer_renderer_get_device([IOSAppDelegate GetDelegate].SwiftLayer);
#elif PLATFORM_IOS
	MTL::Device* Device = [IOSAppDelegate GetDelegate].IOSView->MetalDevice;
#else
	MTL::Device* Device = GetMTLDevice(DeviceIndex);
	if (!Device)
	{
		FPlatformMisc::MessageBoxExt(EAppMsgType::Ok, TEXT("The graphics card in this Mac appears to erroneously report support for Metal graphics technology, which is required to run this application, but failed to create a Metal device. The application will now exit."), TEXT("Failed to initialize Metal"));
		exit(0);
	}
#endif
	
	uint32 MetalDebug = GMetalRuntimeDebugLevel;
	const bool bOverridesMetalDebug = FParse::Value( FCommandLine::Get(), TEXT( "MetalRuntimeDebugLevel=" ), MetalDebug );
	if (bOverridesMetalDebug)
	{
		GMetalRuntimeDebugLevel = MetalDebug;
	}
	
	FMetalDevice* MetalDevice = new FMetalDevice(Device, DeviceIndex);
	
	if (MetalDevice->SupportsFeature(EMetalFeaturesFences))
	{
		FMetalFencePool::Get().Initialise(Device);
	}
	
	return MetalDevice;
}

FMetalDevice::FMetalDevice(MTL::Device* MetalDevice, uint32 InDeviceIndex)
	: Device(MetalDevice)
	, DeviceIndex(InDeviceIndex)
	, Heap(*this)
	, FrameCounter(0)
	, PSOManager(0)
	, FrameNumberRHIThread(0)
{
	Device->retain();
		
	EnumerateFeatureSupport();
	
	CommandQueue = new FMetalCommandQueue(*this, GMetalCommandQueueSize);
	check(CommandQueue);
		
	RuntimeDebuggingLevel = GMetalRuntimeDebugLevel;
	
	CaptureManager = new FMetalCaptureManager(MetalDevice, *CommandQueue);
	
	// If the separate present thread is enabled then an intermediate backbuffer is required
	check(!GMetalSeparatePresentThread || GMetalSupportsIntermediateBackBuffer);
	
	// Hook into the ios framepacer, if it's enabled for this platform.
	FrameReadyEvent = NULL;
	if( FPlatformRHIFramePacer::IsEnabled() || GMetalSeparatePresentThread )
	{
		FrameReadyEvent = FPlatformProcess::GetSynchEventFromPool();
		FPlatformRHIFramePacer::InitWithEvent( FrameReadyEvent );
		
		// A bit dirty - this allows the present frame pacing to match the CPU pacing by default unless you've overridden it with the CVar
		// In all likelihood the CVar is only useful for debugging.
		if (GMetalPresentFramePacing <= 0.0f)
		{
			FString FrameRateLockAsEnum;
			GConfig->GetString(TEXT("/Script/IOSRuntimeSettings.IOSRuntimeSettings"), TEXT("FrameRateLock"), FrameRateLockAsEnum, GEngineIni);
	
			uint32 FrameRateLock = 0;
			FParse::Value(*FrameRateLockAsEnum, TEXT("PUFRL_"), FrameRateLock);
			if (FrameRateLock > 0)
			{
				GMetalPresentFramePacing = (float)FrameRateLock;
			}
		}
	}
	
    const bool bIsVisionOS = PLATFORM_VISIONOS;
	if (bIsVisionOS || FParse::Param(FCommandLine::Get(), TEXT("MetalIntermediateBackBuffer")) || FParse::Param(FCommandLine::Get(), TEXT("MetalOffscreenOnly")))
	{
		GMetalSupportsIntermediateBackBuffer = 1;
	}
    
    // initialize uniform and transfer allocators
    UniformBufferAllocator = new FMetalTempAllocator(*this, GMetalDefaultUniformBufferAllocation, GMetalTargetUniformAllocationLimit, BufferOffsetAlignment);
	TransferBufferAllocator = new FMetalTempAllocator(*this, GMetalDefaultTransferAllocation, GMetalTargetTransferAllocatorLimit, BufferBackedLinearTextureOffsetAlignment);
	
	PSOManager = new FMetalPipelineStateCacheManager(*this);
	
#if METAL_RHI_RAYTRACING
	InitializeRayTracing();
#endif
#if PLATFORM_SUPPORTS_BINDLESS_RENDERING
    BindlessDescriptorManager = new FMetalBindlessDescriptorManager(*this);
#endif
	
	Heap.Init(GetCommandQueue());
	
	FrameSemaphore = dispatch_semaphore_create(FParse::Param(FCommandLine::Get(),TEXT("gpulockstep")) ? 1 : 3);
}

FMetalDevice::~FMetalDevice()
{
	FRHICommandListImmediate& RHICmdList = FRHICommandListImmediate::Get();
	RHICmdList.SubmitAndBlockUntilGPUIdle();
	
	delete &(GetCommandQueue());
	delete PSOManager;
    delete UniformBufferAllocator;
	delete CaptureManager;
	
    ShutdownPipelineCache();
    
#if METAL_RHI_RAYTRACING
	CleanUpRayTracing();
#endif
#if PLATFORM_SUPPORTS_BINDLESS_RENDERING
    delete BindlessDescriptorManager;
#endif
	
#if PLATFORM_MAC
    MTL::RemoveDeviceObserver(GMetalDeviceObserver);
#endif
	
	Device->release();
}

void FMetalDevice::EnumerateFeatureSupport()
{
#if PLATFORM_MAC
	const TCHAR* const Settings = TEXT("/Script/MacTargetPlatform.MacTargetSettings");
#else
	const TCHAR* const Settings = TEXT("/Script/IOSRuntimeSettings.IOSRuntimeSettings");
#endif
	
	int32 IndirectArgumentTier = 0;
	
	if(!GConfig->GetInt(Settings, TEXT("IndirectArgumentTier"), IndirectArgumentTier, GEngineIni))
	{
		IndirectArgumentTier = 0;
	}
	
#if PLATFORM_IOS
#if PLATFORM_TVOS
	Features &= ~(EMetalFeaturesSetBytes);
		
	if(Device->supportsFeatureSet(MTL::FeatureSet_tvOS_GPUFamily2_v1))
	{
		Features |= EMetalFeaturesCountingQueries | EMetalFeaturesBaseVertexInstance | EMetalFeaturesIndirectBuffer | EMetalFeaturesMSAADepthResolve | EMetalFeaturesMSAAStoreAndResolve;
	}

	Features |= EMetalFeaturesPrivateBufferSubAllocation;

	Features |= EMetalFeaturesGPUCaptureManager | EMetalFeaturesBufferSubAllocation | EMetalFeaturesParallelRenderEncoders | EMetalFeaturesPipelineBufferMutability;

	Features |= EMetalFeaturesMaxThreadsPerThreadgroup;

	if (FParse::Param(FCommandLine::Get(), TEXT("metalfence")))
	{
		Features |= EMetalFeaturesFences;
	}
					
	if (FParse::Param(FCommandLine::Get(),TEXT("metalheap")))
	{
		Features |= EMetalFeaturesHeaps;
	}

	Features |= EMetalFeaturesTextureBuffers;
#else
	if (Device->supportsFeatureSet(MTL::FeatureSet_iOS_GPUFamily3_v1))
	{
		Features |= EMetalFeaturesCountingQueries | EMetalFeaturesBaseVertexInstance | EMetalFeaturesIndirectBuffer | EMetalFeaturesMSAADepthResolve;
	}
		
	if(Device->supportsFeatureSet(MTL::FeatureSet_iOS_GPUFamily3_v2) || Device->supportsFeatureSet(MTL::FeatureSet_iOS_GPUFamily2_v3) || Device->supportsFeatureSet(MTL::FeatureSet_iOS_GPUFamily1_v3))
	{
		if (FParse::Param(FCommandLine::Get(),TEXT("metalfence")))
		{
			Features |= EMetalFeaturesFences;
		}
			
		if (FParse::Param(FCommandLine::Get(),TEXT("metalheap")))
		{
			Features |= EMetalFeaturesHeaps;
		}
	}
		
	if(Device->supportsFeatureSet(MTL::FeatureSet_iOS_GPUFamily3_v2))
	{
		Features |= EMetalFeaturesMSAAStoreAndResolve;
	}

	// Turning the below option on will allocate more buffer memory which isn't generally desirable on iOS
	// Features |= EMetalFeaturesEfficientBufferBlits;
			
	// These options are fine however as thye just change how we allocate small buffers
	Features |= EMetalFeaturesBufferSubAllocation;
	Features |= EMetalFeaturesPrivateBufferSubAllocation;

	Features |= EMetalFeaturesPresentMinDuration | EMetalFeaturesGPUCaptureManager | EMetalFeaturesBufferSubAllocation | EMetalFeaturesParallelRenderEncoders | EMetalFeaturesPipelineBufferMutability;

	Features |= EMetalFeaturesMaxThreadsPerThreadgroup;
	if (!GForceNoMetalFence && !FParse::Param(FCommandLine::Get(), TEXT("nometalfence")))
	{
		Features |= EMetalFeaturesFences;
	}
					
	if (!GForceNoMetalHeap && !FParse::Param(FCommandLine::Get(),TEXT("nometalheap")))
	{
		Features |= EMetalFeaturesHeaps;
	}

	Features |= EMetalFeaturesTextureBuffers;

	if (Device->supportsFeatureSet(MTL::FeatureSet_iOS_GPUFamily4_v1))
	{
		Features |= EMetalFeaturesTileShaders;
	}
					
	if (Device->supportsFeatureSet(MTL::FeatureSet_iOS_GPUFamily5_v1))
	{
		Features |= EMetalFeaturesLayeredRendering;
	}
#endif
#else // Assume that Mac & other platforms all support these from the start. They can diverge later.
	Features = EMetalFeaturesCountingQueries | EMetalFeaturesBaseVertexInstance | EMetalFeaturesIndirectBuffer |
				EMetalFeaturesLayeredRendering | EMetalFeaturesCubemapArrays | EMetalFeaturesSetBufferOffset;

	FString DeviceName(Device->name()->cString(NS::UTF8StringEncoding));
	
	Features |= EMetalFeaturesMSAADepthResolve | EMetalFeaturesMSAAStoreAndResolve;
	
	// Assume that set*Bytes only works on macOS Sierra and above as no-one has tested it anywhere else.
	Features |= EMetalFeaturesSetBytes;
	
	// On earlier OS versions Intel Broadwell couldn't suballocate properly
	if (!(DeviceName.Contains(TEXT("Intel")) && (DeviceName.Contains(TEXT("5300")) || DeviceName.Contains(TEXT("6000")) || DeviceName.Contains(TEXT("6100")))))
	{
		// Using Private Memory & BlitEncoders for Vertex & Index data should be *much* faster.
		Features |= EMetalFeaturesEfficientBufferBlits;
		
		Features |= EMetalFeaturesBufferSubAllocation;
				
		// On earlier OS versions Vega didn't like non-zero blit offsets
		if (!DeviceName.Contains(TEXT("Vega")))
		{
			Features |= EMetalFeaturesPrivateBufferSubAllocation;
		}
	}
	
	if (!FParse::Param(FCommandLine::Get(), TEXT("nometalparallelencoder")))
	{
		Features |= EMetalFeaturesParallelRenderEncoders;
	}
	Features |= EMetalFeaturesTextureBuffers;
	if (IndirectArgumentTier >= 1)
	{
		Features |= EMetalFeaturesIABs;
			
		if (IndirectArgumentTier >= 2)
		{
			Features |= EMetalFeaturesTier2IABs;
		}
	}

	// The editor spawns so many viewports and preview icons that we can run out of hardware fences!
	// Need to figure out a way to safely flush the rendering and reuse the fences when that happens.
#if WITH_EDITORONLY_DATA
	if (!GIsEditor)
#endif
	{
		if (FParse::Param(FCommandLine::Get(),TEXT("metalfence")))
		{
			Features |= EMetalFeaturesFences;
		}
	}
	
	// Temporarily only support heaps for devices with unified memory
	// Disable this by default code while we work on metal heaps
	if (!DeviceName.Contains(TEXT("Intel")) &&
		  Device->hasUnifiedMemory() &&
		  FParse::Param(FCommandLine::Get(),TEXT("metalheap")))
	{
		Features |= EMetalFeaturesHeaps;
	}
	
	Features |= EMetalFeaturesMultipleViewports | EMetalFeaturesPipelineBufferMutability | EMetalFeaturesGPUCaptureManager;
	
	if (FParse::Param(FCommandLine::Get(),TEXT("metalfence")))
	{
		Features |= EMetalFeaturesFences;
	}
	
	if (FParse::Param(FCommandLine::Get(),TEXT("metaliabs")))
	{
		Features |= EMetalFeaturesIABs;
	}
#endif
	
#if !UE_BUILD_SHIPPING
	Class MTLDebugDevice = NSClassFromString(@"MTLDebugDevice");
	id<MTLDevice> ObjCDevice = (__bridge id<MTLDevice>)Device;
	if ([ObjCDevice isKindOfClass:MTLDebugDevice])
	{
		Features |= EMetalFeaturesValidation;
	}
#endif

	static const auto CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Shaders.Optimize"));
	if (CVar->GetInt() == 0 || FParse::Param(FCommandLine::Get(),TEXT("metalshaderdebug")))
	{
		Features |= EMetalFeaturesGPUTrace;
	}
}

void FMetalDevice::EndDrawingViewport(bool bPresent)
{
	// We may be limiting our framerate to the display link
	if( FrameReadyEvent != nullptr && !GMetalSeparatePresentThread )
	{
		bool bIgnoreThreadIdleStats = true; // Idle time is already counted by the caller
		FrameReadyEvent->Wait(MAX_uint32, bIgnoreThreadIdleStats);
	}
	
	if(bPresent)
	{
		CaptureManager->PresentFrame(FrameCounter++);
	}
}

void FMetalDevice::DrainHeap()
{
	Heap.Compact(false);
}

void FMetalDevice::GarbageCollect()
{
	DrainHeap();
	
	TransferBufferAllocator->Cleanup();
	UniformBufferAllocator->Cleanup();
}

MTLTexturePtr FMetalDevice::CreateTexture(FMetalSurface* Surface, MTL::TextureDescriptor* Descriptor)
{
	MTLTexturePtr Tex = Heap.CreateTexture(Descriptor, Surface);
#if METAL_DEBUG_OPTIONS
	if (GMetalResourcePurgeOnDelete && !Tex->heap())
	{
		Tex->setPurgeableState(MTL::PurgeableStateNonVolatile);
	}
#endif
	
	return Tex;
}

FMetalBufferPtr FMetalDevice::CreatePooledBuffer(FMetalPooledBufferArgs const& Args)
{
	NS::UInteger CpuResourceOption = ((NS::UInteger)Args.CpuCacheMode) << MTL::ResourceCpuCacheModeShift;
	
	uint32 RequestedBufferOffsetAlignment = BufferOffsetAlignment;
	
	if(EnumHasAnyFlags(Args.Flags, BUF_UnorderedAccess | BUF_ShaderResource))
	{
		// Buffer backed linear textures have specific align requirements
		// We don't know upfront the pixel format that may be requested for an SRV so we can't use minimumLinearTextureAlignmentForPixelFormat:
		RequestedBufferOffsetAlignment = BufferBackedLinearTextureOffsetAlignment;
	}
	
	MTL::ResourceOptions HazardTrackingMode = MTL::ResourceHazardTrackingModeUntracked;
	static bool bSupportsHeaps = SupportsFeature(EMetalFeaturesHeaps);
	if(bSupportsHeaps)
	{
		HazardTrackingMode = MTL::ResourceHazardTrackingModeTracked;
	}
	
    FMetalBufferPtr Buffer = Heap.CreateBuffer(Args.Size, RequestedBufferOffsetAlignment, Args.Flags, FMetalCommandQueue::GetCompatibleResourceOptions((MTL::ResourceOptions)(CpuResourceOption | HazardTrackingMode | ((NS::UInteger)Args.Storage << MTL::ResourceStorageModeShift))));
	
    check(Buffer);
#if METAL_DEBUG_OPTIONS
    MTL::Buffer* MTLBuffer = Buffer->GetMTLBuffer();
	if (GMetalResourcePurgeOnDelete && !MTLBuffer->heap())
	{
        MTLBuffer->setPurgeableState(MTL::PurgeableStateNonVolatile);
	}
#endif
	
	return Buffer;
}

MTLEventPtr FMetalDevice::CreateEvent()
{
	MTLEventPtr Event = NS::TransferPtr(Device->newEvent());
	return Event;
}

uint32 FMetalDevice::GetDeviceIndex(void) const
{
	return DeviceIndex;
}

#if METAL_DEBUG_OPTIONS
void FMetalDevice::AddActiveBuffer(MTL::Buffer* Buffer, const NS::Range& Range)
{
    if(GetRuntimeDebuggingLevel() >= EMetalDebugLevelValidation)
    {
        FScopeLock Lock(&ActiveBuffersMutex);
        
        NS::Range DestRange = NS::Range::Make(Range.location, Range.length);
        TArray<NS::Range>* Ranges = ActiveBuffers.Find(Buffer);
        if (!Ranges)
        {
            ActiveBuffers.Add(Buffer, TArray<NS::Range>());
            Ranges = ActiveBuffers.Find(Buffer);
        }
        Ranges->Add(DestRange);
    }
}

static bool operator==(NSRange const& A, NSRange const& B)
{
    return NSEqualRanges(A, B);
}

void FMetalDevice::RemoveActiveBuffer(MTL::Buffer* Buffer, const NS::Range& Range)
{
    if(GetRuntimeDebuggingLevel() >= EMetalDebugLevelValidation)
    {
        FScopeLock Lock(&ActiveBuffersMutex);
        
        TArray<NS::Range>& Ranges = ActiveBuffers.FindChecked(Buffer);
        int32 i = Ranges.RemoveSingle(Range);
        check(i > 0);
    }
}

bool FMetalDevice::ValidateIsInactiveBuffer(MTL::Buffer* Buffer, const NS::Range& DestRange)
{
    if(GetRuntimeDebuggingLevel() >= EMetalDebugLevelValidation)
    {
        FScopeLock Lock(&ActiveBuffersMutex);
        
        TArray<NS::Range>* Ranges = ActiveBuffers.Find(Buffer);
        if (Ranges)
        {
            for (NS::Range Range : *Ranges)
            {
                if(DestRange.location < Range.location + Range.length ||
                   Range.location < DestRange.location + DestRange.length)
                {
                    continue;
                }
                
                UE_LOG(LogMetal, Error, TEXT("ValidateIsInactiveBuffer failed on overlapping ranges ({%d, %d} vs {%d, %d}) of buffer %p."), (uint32)Range.location, (uint32)Range.length, (uint32)DestRange.location, (uint32)DestRange.length, Buffer);
                return false;
            }
        }
    }
    return true;
}
#endif
