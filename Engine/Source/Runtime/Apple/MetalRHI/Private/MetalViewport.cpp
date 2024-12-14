// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MetalViewport.cpp: Metal viewport RHI implementation.
=============================================================================*/

#include "MetalViewport.h"
#include "MetalDynamicRHI.h"
#include "MetalRHIPrivate.h"
#include "MetalCommandBuffer.h"
#include "MetalProfiler.h"
#include "MetalRHIVisionOSBridge.h"
#include "MetalDevice.h"

#import <QuartzCore/CAMetalLayer.h>

#if PLATFORM_MAC
#include "Mac/CocoaWindow.h"
#include "Mac/CocoaThread.h"
#else
#include "IOS/IOSAppDelegate.h"
#endif
#include "RenderCommandFence.h"
#include "Containers/Set.h"
#include "RenderUtils.h"
#include "Engine/RendererSettings.h"

extern int32 GMetalSupportsIntermediateBackBuffer;
extern int32 GMetalSeparatePresentThread;
extern float GMetalPresentFramePacing;

#if PLATFORM_IOS
static int32 GEnablePresentPacing = 0;
static FAutoConsoleVariableRef CVarMetalEnablePresentPacing(
	   TEXT("ios.PresentPacing"),
	   GEnablePresentPacing,
	   TEXT(""),
		ECVF_Default);
#endif


int32 GMetalNonBlockingPresent = 0;
static FAutoConsoleVariableRef CVarMetalNonBlockingPresent(
	TEXT("rhi.Metal.NonBlockingPresent"),
	GMetalNonBlockingPresent,
	TEXT("When enabled (> 0) this will force MetalRHI to query if a back-buffer is available to present and if not will skip the frame. Only functions on macOS, it is ignored on iOS/tvOS.\n")
	TEXT("(Off by default (0))"));

#if PLATFORM_MAC

// Quick way to disable availability warnings is to duplicate the definitions into a new type - gotta love ObjC dynamic-dispatch!
@interface FCAMetalLayer : CALayer
@property BOOL displaySyncEnabled;
@property BOOL allowsNextDrawableTimeout;
@end

@implementation FMetalView

- (id)initWithFrame:(NSRect)frameRect
{
	self = [super initWithFrame:frameRect];
	if (self)
	{
	}
	return self;
}

- (BOOL)isOpaque
{
	return YES;
}

- (BOOL)mouseDownCanMoveWindow
{
	return YES;
}

@end

#endif

static FCriticalSection ViewportsMutex;
static TSet<FMetalViewport*> Viewports;

FMetalViewport::FMetalViewport(FMetalDevice& InDevice, void* WindowHandle, uint32 InSizeX, uint32 InSizeY, bool bInIsFullscreen, EPixelFormat Format)
	: Device(InDevice)
	, Drawable{nullptr}
	, BackBuffer{nullptr, nullptr}
	, Mutex{}
	, DrawableTextures{}
	, DisplayID{0}
	, Block{nullptr}
	, FrameAvailable{0}
	, LastCompleteFrame{nullptr}
	, bIsFullScreen{bInIsFullscreen}
#if PLATFORM_MAC
	, View{nullptr}
#endif
#if PLATFORM_MAC || PLATFORM_VISIONOS
	, CustomPresent{nullptr}
#endif
{
#if PLATFORM_VISIONOS
	// look to see if we need to hook up to a Swift compositor renderer
	SwiftLayer = [IOSAppDelegate GetDelegate].SwiftLayer;
#endif

#if PLATFORM_MAC
	MainThreadCall(^{
		FCocoaWindow* Window = (FCocoaWindow*)WindowHandle;
		const NSRect ContentRect = NSMakeRect(0, 0, InSizeX, InSizeY);
		View = [[FMetalView alloc] initWithFrame:ContentRect];
		[View setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
		[View setWantsLayer:YES];

		CAMetalLayer* Layer = [CAMetalLayer new];

		CGFloat bgColor[] = { 0.0, 0.0, 0.0, 0.0 };
		Layer.edgeAntialiasingMask = 0;
		Layer.masksToBounds = YES;
		Layer.backgroundColor = CGColorCreate(CGColorSpaceCreateDeviceRGB(), bgColor);
		Layer.presentsWithTransaction = NO;
		Layer.anchorPoint = CGPointMake(0.5, 0.5);
		Layer.frame = ContentRect;
		Layer.magnificationFilter = kCAFilterNearest;
		Layer.minificationFilter = kCAFilterNearest;

		[Layer setDevice:(__bridge id<MTLDevice>)Device.GetDevice()];
		
		[Layer setFramebufferOnly:NO];
		[Layer removeAllAnimations];

		[View setLayer:Layer];

		[Window setContentView:View];
		[[Window standardWindowButton:NSWindowCloseButton] setAction:@selector(performClose:)];
	}, NSDefaultRunLoopMode, true);
#endif
	Resize(InSizeX, InSizeY, bInIsFullscreen, Format);
	
	{
		FScopeLock Lock(&ViewportsMutex);
		Viewports.Add(this);
	}
}

FMetalViewport::~FMetalViewport()
{
	if (Block)
	{
		FScopeLock BlockLock(&Mutex);
		if (GMetalSeparatePresentThread)
		{
			FPlatformRHIFramePacer::RemoveHandler(Block);
		}
		Block_release(Block);
		Block = nullptr;
	}
	{
		FScopeLock Lock(&ViewportsMutex);
		Viewports.Remove(this);
	}
	
	BackBuffer[0].SafeRelease();	// when the rest of the engine releases it, its framebuffers will be released too (those the engine knows about)
	BackBuffer[1].SafeRelease();
	check(!IsValidRef(BackBuffer[0]));
	check(!IsValidRef(BackBuffer[1]));
}

uint32 FMetalViewport::GetViewportIndex(EMetalViewportAccessFlag Accessor) const
{
	switch(Accessor)
	{
		case EMetalViewportAccessRHI:
			check(IsInParallelRenderingThread());
			// Deliberate fall-through
		case EMetalViewportAccessDisplayLink: // Displaylink is not an index, merely an alias that avoids the check...
			return (GRHISupportsRHIThread && IsRunningRHIInSeparateThread()) ? EMetalViewportAccessRHI : EMetalViewportAccessRenderer;
		case EMetalViewportAccessRenderer:
			check(IsInRenderingThread());
			return Accessor;
		case EMetalViewportAccessGame:
			check(IsInGameThread());
			return EMetalViewportAccessRenderer;
		default:
			check(false);
			return EMetalViewportAccessRenderer;
	}
}

void FMetalViewport::Resize(uint32 InSizeX, uint32 InSizeY, bool bInIsFullscreen, EPixelFormat Format)
{
	bIsFullScreen = bInIsFullscreen;
	uint32 Index = GetViewportIndex(EMetalViewportAccessGame);
	
	bool bUseHDR = GRHISupportsHDROutput && Format == GRHIHDRDisplayOutputFormat;
	
    MTL::PixelFormat MetalFormat = (MTL::PixelFormat)GPixelFormats[Format].PlatformFormat;
	
    ENQUEUE_RENDER_COMMAND(FlushPendingRHICommands)(
        [Viewport = this](FRHICommandListImmediate& RHICmdList)
        {
            GRHICommandList.GetImmediateCommandList().BlockUntilGPUIdle();
        });
    
	if (IsValidRef(BackBuffer[Index]) && Format != BackBuffer[Index]->GetFormat())
	{
		// Really need to flush the RHI thread & GPU here...
		AddRef();
		ENQUEUE_RENDER_COMMAND(FlushPendingRHICommands)(
			[Viewport = this](FRHICommandListImmediate& RHICmdList)
			{
				Viewport->ReleaseDrawable();
				Viewport->Release();
			});
	}
    
    // Issue a fence command to the rendering thread and wait for it to complete.
    FRenderCommandFence Fence;
    Fence.BeginFence();
    Fence.Wait();
    
#if PLATFORM_MAC
	MainThreadCall(^
	{
		CAMetalLayer* MetalLayer = (CAMetalLayer*)View.layer;
		
		MetalLayer.drawableSize = CGSizeMake(InSizeX, InSizeY);
		
		if (MetalFormat != (MTL::PixelFormat)MetalLayer.pixelFormat)
		{
			MetalLayer.pixelFormat = (MTLPixelFormat)MetalFormat;
		}
		
		if (bUseHDR != MetalLayer.wantsExtendedDynamicRangeContent)
		{
			MetalLayer.wantsExtendedDynamicRangeContent = bUseHDR;
		}
		
	}, NSDefaultRunLoopMode, true);
#else
	// A note on HDR in iOS
	// Setting the pixel format to one of the Apple XR formats is all you need.
	// iOS expects the app to output in sRGB regadless of the display
	// (even though Apple's HDR displays are P3)
	// and its compositor will do the conversion.
	{
		dispatch_sync(dispatch_get_main_queue(), ^{
			IOSAppDelegate* AppDelegate = [IOSAppDelegate GetDelegate];
			FIOSView* IOSView = AppDelegate.IOSView;
			
			CAMetalLayer* MetalLayer = (CAMetalLayer*) IOSView.layer;
			
			if (MetalFormat != (MTL::PixelFormat) MetalLayer.pixelFormat)
			{
				MetalLayer.pixelFormat = (MTLPixelFormat) MetalFormat;
			}
			
			[IOSView UpdateRenderWidth:InSizeX andHeight:InSizeY];
		});
	}
#endif

    {
        FScopeLock Lock(&Mutex);

		TRefCountPtr<FMetalSurface> NewBackBuffer;
		TRefCountPtr<FMetalSurface> DoubleBuffer;

		FRHITextureCreateDesc CreateDesc =
			FRHITextureCreateDesc::Create2D(TEXT("BackBuffer"), InSizeX, InSizeY, Format)
			.SetClearValue(FClearValueBinding::None)
			.SetFlags(ETextureCreateFlags::RenderTargetable);
		
		if (!GMetalSupportsIntermediateBackBuffer)
		{
			CreateDesc.AddFlags(ETextureCreateFlags::Presentable);
		}
		
		CreateDesc.SetInitialState(RHIGetDefaultResourceState(CreateDesc.Flags, false));

		NewBackBuffer = new FMetalSurface(Device, nullptr, FMetalTextureCreateDesc(Device, CreateDesc));
		NewBackBuffer->Viewport = this;

        if (GMetalSupportsIntermediateBackBuffer && GMetalSeparatePresentThread)
        {
            DoubleBuffer = new FMetalSurface(Device, nullptr, FMetalTextureCreateDesc(Device, CreateDesc));
            DoubleBuffer->Viewport = this;
        }

        BackBuffer[Index] = NewBackBuffer;
        if (GMetalSeparatePresentThread)
        {
            BackBuffer[EMetalViewportAccessRHI] = DoubleBuffer;
        }
        else
        {
            BackBuffer[EMetalViewportAccessRHI] = BackBuffer[Index];
        }
    }
}

TRefCountPtr<FMetalSurface> FMetalViewport::GetBackBuffer(EMetalViewportAccessFlag Accessor) const
{
	FScopeLock Lock(&Mutex);
	
	uint32 Index = GetViewportIndex(Accessor);
	check(IsValidRef(BackBuffer[Index]));
	return BackBuffer[Index];
}

#if PLATFORM_MAC
@protocol CAMetalLayerSPI <NSObject>
- (BOOL)isDrawableAvailable;
@end
#endif

CA::MetalDrawable* FMetalViewport::GetDrawable(EMetalViewportAccessFlag Accessor)
{
#if PLATFORM_VISIONOS
	// no CAMetalDrawable in Swift mode
	if (SwiftLayer != nullptr)
	{
		return nullptr;
	}
#endif
	
	SCOPE_CYCLE_COUNTER(STAT_MetalMakeDrawableTime);
    if (!Drawable || (Drawable->texture()->width() != BackBuffer[GetViewportIndex(Accessor)]->GetSizeX() ||
                      Drawable->texture()->height() != BackBuffer[GetViewportIndex(Accessor)]->GetSizeY()))
	{
		// Drawable changed, release the previously retained object.
		if (Drawable != nullptr)
		{
			Drawable->release();
			Drawable = nullptr;
		}

        MTL_SCOPED_AUTORELEASE_POOL;
        {
            FRenderThreadIdleScope IdleScope(ERenderThreadIdleTypes::WaitingForGPUPresent);

#if PLATFORM_MAC
            CA::MetalLayer* CurrentLayer = (__bridge CA::MetalLayer*)[View layer];
            if (GMetalNonBlockingPresent == 0 || [((id<CAMetalLayerSPI>)CurrentLayer) isDrawableAvailable])
            {
                Drawable = CurrentLayer ? CurrentLayer->nextDrawable() : nullptr;
            }

#if METAL_DEBUG_OPTIONS
            if (Drawable)
            {
                CGSize Size = Drawable->layer()->drawableSize();
                if ((Size.width != BackBuffer[GetViewportIndex(Accessor)]->GetSizeX() || Size.height != BackBuffer[GetViewportIndex(Accessor)]->GetSizeY()))
                {
                    UE_LOG(LogMetal, Display, TEXT("Viewport Size Mismatch: Drawable W:%f H:%f, Viewport W:%u H:%u"), Size.width, Size.height, BackBuffer[GetViewportIndex(Accessor)]->GetSizeX(), BackBuffer[GetViewportIndex(Accessor)]->GetSizeY());
                }
            }
#endif // METAL_DEBUG_OPTIONS

#else // PLATFORM_MAC
            CGSize Size;
            IOSAppDelegate* AppDelegate = [IOSAppDelegate GetDelegate];
            do
            {
                Drawable = (__bridge CA::MetalDrawable*)[AppDelegate.IOSView MakeDrawable];
                if (Drawable != nullptr)
                {
                    Size.width = Drawable->texture()->width();
                    Size.height = Drawable->texture()->height();
                }
                else
                {
                    FPlatformProcess::SleepNoStats(0.001f);
                }
            }
            while (Drawable == nullptr || Size.width != BackBuffer[GetViewportIndex(Accessor)]->GetSizeX() || Size.height != BackBuffer[GetViewportIndex(Accessor)]->GetSizeY());

#endif // PLATFORM_MAC
        }

        // Retain the drawable here or it will be released when the
        // autorelease pool goes out of scope.
        if (Drawable != nullptr)
        {
            Drawable->retain();
        }
	}

	return Drawable;
}

MTL::Texture* FMetalViewport::GetDrawableTexture(EMetalViewportAccessFlag Accessor)
{
	CA::MetalDrawable* CurrentDrawable = GetDrawable(Accessor);
    uint32 Index = GetViewportIndex(Accessor);
    
#if METAL_DEBUG_OPTIONS
    MTL_SCOPED_AUTORELEASE_POOL;

#if PLATFORM_MAC
    CAMetalLayer* CurrentLayer = (CAMetalLayer*)[View layer];
#else
    CAMetalLayer* CurrentLayer = (CAMetalLayer*)[[IOSAppDelegate GetDelegate].IOSView layer];
#endif
    
    CGSize Size = CurrentLayer.drawableSize;
    if (CurrentDrawable->texture()->width() != BackBuffer[Index]->GetSizeX() || CurrentDrawable->texture()->height() != BackBuffer[Index]->GetSizeY())
    {
        UE_LOG(LogMetal, Display, TEXT("Viewport Size Mismatch: Drawable W:%f H:%f, Texture W:%llu H:%llu, Viewport W:%u H:%u"), Size.width, Size.height, CurrentDrawable->texture()->height(), CurrentDrawable->texture()->height(), BackBuffer[Index]->GetSizeX(), BackBuffer[Index]->GetSizeY());
    }
#endif
    
	DrawableTextures[Index] = CurrentDrawable->texture();
	return CurrentDrawable->texture();
}

MTL::Texture* FMetalViewport::GetCurrentTexture(EMetalViewportAccessFlag Accessor)
{
	uint32 Index = GetViewportIndex(Accessor);
	return DrawableTextures[Index];
}

void FMetalViewport::ReleaseDrawable()
{
	if (!GMetalSeparatePresentThread)
	{
		if (Drawable != nullptr)
		{
			Drawable->release();
			Drawable = nullptr;
		}

		if (!GMetalSupportsIntermediateBackBuffer && IsValidRef(BackBuffer[GetViewportIndex(EMetalViewportAccessRHI)]))
		{
			BackBuffer[GetViewportIndex(EMetalViewportAccessRHI)]->Texture.reset();
		}
	}
}

#if PLATFORM_MAC
NSWindow* FMetalViewport::GetWindow() const
{
	return [View window];
}
#endif

void FMetalViewport::Present(FMetalCommandQueue& CommandQueue, bool bLockToVsync)
{
	FScopeLock Lock(&Mutex);
	
	bool bIsLiveResize = false;
#if PLATFORM_MAC
	NSNumber* ScreenId = [View.window.screen.deviceDescription objectForKey:@"NSScreenNumber"];
	DisplayID = ScreenId.unsignedIntValue;
	bIsLiveResize = View.inLiveResize;
	{
		FCAMetalLayer* CurrentLayer = (FCAMetalLayer*)[View layer];
		CurrentLayer.displaySyncEnabled = bLockToVsync || (!(IsRunningGame() && bIsFullScreen));
	}
#endif
	
	LastCompleteFrame = GetBackBuffer(EMetalViewportAccessRHI);
	FPlatformAtomics::InterlockedExchange(&FrameAvailable, 1);
	
	if (!Block)
	{
		Block = Block_copy(^(uint32 InDisplayID, double OutputSeconds, double OutputDuration)
		{
#if !PLATFORM_MAC
			uint32 FramePace = FPlatformRHIFramePacer::GetFramePace();
			float MinPresentDuration = FramePace ? (1.0f / (float)FramePace) : 0.0f;
#endif
			bool bIsInLiveResize = false;
#if PLATFORM_MAC
			bIsInLiveResize = View.inLiveResize;
#endif
			if (FrameAvailable > 0 && (InDisplayID == 0 || (DisplayID == InDisplayID && !bIsInLiveResize)))
			{
				FPlatformAtomics::InterlockedDecrement(&FrameAvailable);
				CA::MetalDrawable* LocalDrawable = GetDrawable(EMetalViewportAccessDisplayLink);
                LocalDrawable->retain();
				MTL::Texture* DrawableTexture = GetDrawableTexture(EMetalViewportAccessDisplayLink);
				
				{
					FScopeLock BlockLock(&Mutex);
#if PLATFORM_MAC
					bIsInLiveResize = View.inLiveResize;
#endif
					
					if (DrawableTexture && (InDisplayID == 0 || !bIsInLiveResize))
					{
						FMetalCommandBuffer* CurrentCommandBuffer = CommandQueue.CreateCommandBuffer();
						check(CurrentCommandBuffer);
						
#if ENABLE_METAL_GPUPROFILE
						FMetalProfiler* Profiler = FMetalProfiler::GetProfiler();
						FMetalCommandBufferStats* Stats = Profiler->AllocateCommandBuffer(CurrentCommandBuffer->GetMTLCmdBuffer(), 0);
#endif
						
						if (GMetalSupportsIntermediateBackBuffer)
						{
							TRefCountPtr<FMetalSurface> Texture = LastCompleteFrame;
							check(IsValidRef(Texture));
							
							MTLTexturePtr Src = Texture->Texture;
                            MTLTexturePtr Dst = NS::RetainPtr(DrawableTexture);
							
							NS::UInteger Width = FMath::Min(Src->width(), Dst->width());
							NS::UInteger Height = FMath::Min(Src->height(), Dst->height());
							
							MTLBlitCommandEncoderPtr Encoder = NS::RetainPtr(CurrentCommandBuffer->GetMTLCmdBuffer()->blitCommandEncoder());
							check(Encoder);
							METAL_GPUPROFILE(Profiler->EncodeBlit(Stats, __FUNCTION__));

							Encoder->copyFromTexture(Src.get(), 0, 0, MTL::Origin(0, 0, 0), MTL::Size(Width, Height, 1), Dst.get(), 0, 0, MTL::Origin(0, 0, 0));
							Encoder->endEncoding();
                            
							Drawable->release();
							Drawable = nullptr;
						}
						
						// This is a bit different than the usual pattern.
						// This command buffer here is committed directly, instead of going through
						// FMetalCommandList::Commit. So long as Present() is called within
						// high level RHI BeginFrame/EndFrame this will not fine.
						// Otherwise the recording of the Present time will be offset by one in the
						// FMetalGPUProfiler frame indices.
						
						dispatch_semaphore_t& FrameSemaphore = Device.GetFrameSemaphore();
						dispatch_retain(FrameSemaphore);
						
#if PLATFORM_MAC
						FMetalView* theView = View;
						MTL::HandlerFunction CommandBufferHandler = [LocalDrawable, theView, FrameSemaphore](MTL::CommandBuffer* cmd_buf)
#else
                        MTL::HandlerFunction CommandBufferHandler = [LocalDrawable, FrameSemaphore](MTL::CommandBuffer* cmd_buf)
#endif
						{
							dispatch_semaphore_signal(FrameSemaphore);
							dispatch_release(FrameSemaphore);
							
							FMetalGPUProfiler::RecordPresent(cmd_buf);
							LocalDrawable->release();
#if PLATFORM_MAC
							MainThreadCall(^{
								FCocoaWindow* Window = (FCocoaWindow*)[theView window];
								[Window startRendering];
							}, NSDefaultRunLoopMode, false);
#endif
						};
						
#if PLATFORM_MAC		// Mac needs the older way to present otherwise we end up with bad behaviour of the completion handlers that causes GPU timeouts.
                        MTL::HandlerFunction ScheduledHandler = [LocalDrawable](MTL::CommandBuffer*)
						{
							LocalDrawable->present();
						};
								
						CurrentCommandBuffer->GetMTLCmdBuffer()->addCompletedHandler(CommandBufferHandler);
						CurrentCommandBuffer->GetMTLCmdBuffer()->addScheduledHandler(ScheduledHandler);

#else // PLATFORM_MAC
						CurrentCommandBuffer->GetMTLCmdBuffer()->addCompletedHandler(CommandBufferHandler);

                        {
                            // Queue this on the current command buffer to ensure that all work is committed prior to the present, present only knows about dependencies on committed work.
                            if (MinPresentDuration && GEnablePresentPacing)
                            {
                                CurrentCommandBuffer->GetMTLCmdBuffer()->presentDrawableAfterMinimumDuration(LocalDrawable, 1.0f/(float)FramePace);
                            }
                            else
                            {
                                CurrentCommandBuffer->GetMTLCmdBuffer()->presentDrawable(LocalDrawable);
                            }
                        }
#endif // PLATFORM_MAC
                        METAL_GPUPROFILE(Stats->End(CurrentCommandBuffer->GetMTLCmdBuffer()));
                        CommandQueue.CommitCommandBuffer(CurrentCommandBuffer);
						
						// Wait for the frame semaphore
						dispatch_semaphore_wait(Device.GetFrameSemaphore(), DISPATCH_TIME_FOREVER);
					}
				}
			}
		});
		
		if (GMetalSeparatePresentThread)
		{
			FPlatformRHIFramePacer::AddHandler(Block);
		}
	}
	
	if (bIsLiveResize || !GMetalSeparatePresentThread)
	{
		Block(0, 0.0, 0.0);
	}
	
	if (!(GRHISupportsRHIThread && IsRunningRHIInSeparateThread()))
	{
		Swap();
	}
}

void FMetalViewport::Swap()
{
	if (GMetalSeparatePresentThread)
	{
		FScopeLock Lock(&Mutex);
		
		check(IsValidRef(BackBuffer[0]));
		check(IsValidRef(BackBuffer[1]));
		
		TRefCountPtr<FMetalSurface> BB0 = BackBuffer[0];
		TRefCountPtr<FMetalSurface> BB1 = BackBuffer[1];
		
		BackBuffer[0] = BB1;
		BackBuffer[1] = BB0;
	}
}

#if PLATFORM_VISIONOS
void FMetalViewport::GetDrawableImmersiveTextures(EMetalViewportAccessFlag Accessor, cp_drawable_t SwiftDrawable, MTL::Texture*& OutColorTexture, MTL::Texture*& OutDepthTexture)
{
	check(SwiftDrawable != nullptr);
	
	// get the color texture out and use that with the RHI
	uint32 Index = GetViewportIndex(Accessor);
	uint32 TextureCount = cp_drawable_get_texture_count(SwiftDrawable);
	check(TextureCount = 1);
	OutColorTexture = (__bridge MTL::Texture*)cp_drawable_get_color_texture(SwiftDrawable, 0);
	OutDepthTexture = (__bridge MTL::Texture*)cp_drawable_get_depth_texture(SwiftDrawable, 0);
	DrawableTextures[Index] = OutColorTexture;
}

// This is the present for Immersive visionOS, through the OXRVisionOS plugin.
void FMetalViewport::PresentImmersive(const MetalRHIVisionOS::PresentImmersiveParams* InVisionOSParams)
{
	// The null param case means that we are not really submitting a frame to the compositor.
	if (InVisionOSParams == nullptr)
	{
		FScopeLock Lock(&Mutex);
		
		dispatch_semaphore_t& FrameSemaphore = Device.GetFrameSemaphore();
		dispatch_semaphore_signal(FrameSemaphore);
		return;
	}
	
	const MetalRHIVisionOS::PresentImmersiveParams& VisionOSParams = *InVisionOSParams;
	
	check(SwiftLayer);  // If no SwiftLayer we should not be trying to be immersive.
	check(VisionOSParams.SwiftFrame);

	check(VisionOSParams.RHICommandContext);
	FMetalRHICommandContext& Context = *static_cast<FMetalRHICommandContext*>(VisionOSParams.RHICommandContext);
	
	FScopeLock Lock(&Mutex);
	
	TRefCountPtr<FMetalSurface> MyLastCompleteFrame = GetMetalSurfaceFromRHITexture(VisionOSParams.Texture);
	TRefCountPtr<FMetalSurface> MyLastCompleteDepth = GetMetalSurfaceFromRHITexture(VisionOSParams.Depth);
	{
		MTL::Texture* DrawableTextureParam = nullptr;
		MTL::Texture* DrawableDepthTextureParam = nullptr;
		GetDrawableImmersiveTextures(EMetalViewportAccessDisplayLink, VisionOSParams.SwiftDrawable, DrawableTextureParam, DrawableDepthTextureParam);
		MTLTexturePtr DrawableTexture = NS::RetainPtr(DrawableTextureParam);
		MTLTexturePtr DrawableDepthTexture = NS::RetainPtr(DrawableDepthTextureParam);
		{
			if (DrawableTexture)
			{
				// TODO Currently we are using intermediate back buffer to connect the OXRVisionOS Swapchain to the drawable.
				// I think we could use the drawable directly and avoid this copy.
				check(GMetalSupportsIntermediateBackBuffer);
				if (GMetalSupportsIntermediateBackBuffer)
				{
					{
						TRefCountPtr<FMetalSurface> Texture = MyLastCompleteFrame;
						check(IsValidRef(Texture));
						MTLTexturePtr Src = Texture->Texture;
						MTLTexturePtr& Dst = DrawableTexture;
						
						NSUInteger Width = FMath::Min(Src->width(), Dst->width());
						NSUInteger Height = FMath::Min(Src->height(), Dst->height());
						
						Context.CopyFromTextureToTexture(Src.get(), 0, 0, MTL::Origin(0, 0, 0), MTL::Size(Width, Height, 1), Dst.get(), 0, 0, MTL::Origin(0, 0, 0));
					}

					{
						TRefCountPtr<FMetalSurface> Texture = MyLastCompleteDepth;
						check(IsValidRef(Texture));
						MTLTexturePtr Src = Texture->Texture;
						MTLTexturePtr& Dst = DrawableDepthTexture;
						
						NS::UInteger Width = FMath::Min(Src->width(), Dst->width());
						NS::UInteger Height = FMath::Min(Src->height(), Dst->height());
						
						Context.CopyFromTextureToTexture(Src.get(), 0, 0, MTL::Origin(0, 0, 0), MTL::Size(Width, Height, 1), Dst.get(), 0, 0, MTL::Origin(0, 0, 0));
					}
				}
				
				TArray<FMetalCommandBuffer*> Buffers = Context.Finalize();
				check(Buffers.Num() > 0);
				
				// We need to attach the completion handler and the present signal to the final
				// command buffer
				FMetalCommandBuffer* FinalCommandBuffer = Buffers.Last();
				
#if ENABLE_METAL_GPUPROFILE
				FMetalProfiler* Profiler = FMetalProfiler::GetProfiler();
				FMetalCommandBufferStats* Stats = Profiler->AllocateCommandBuffer(FinalCommandBuffer->GetMTLCmdBuffer(), 0);
#endif
				
				{
					dispatch_semaphore_t& FrameSemaphore = Device.GetFrameSemaphore();
					dispatch_retain(FrameSemaphore);
					MTL::HandlerFunction CommandBufferHandler = [FrameSemaphore](MTL::CommandBuffer* cmd_buf)
					{
						dispatch_semaphore_signal(FrameSemaphore);
						dispatch_release(FrameSemaphore);
						
						FMetalGPUProfiler::RecordPresent(cmd_buf);
					};
					FinalCommandBuffer->GetMTLCmdBuffer()->addCompletedHandler(CommandBufferHandler);
				}
				
				cp_drawable_encode_present(VisionOSParams.SwiftDrawable, (__bridge id<MTLCommandBuffer>)FinalCommandBuffer->GetMTLCmdBuffer().get());
				cp_frame_t CompositorServicesFrame = VisionOSParams.SwiftFrame;

				METAL_GPUPROFILE(Stats->End(FinalCommandBuffer->GetMTLCmdBuffer()));			
				
				for(FMetalCommandBuffer* Buffer : Buffers)
				{
					Context.GetDevice().GetCommandQueue().CommitCommandBuffer(Buffer);
				}
				Context.ResetContext();
				
				cp_frame_end_submission(CompositorServicesFrame);
				
				// Wait for the frame semaphore
				dispatch_semaphore_wait(Device.GetFrameSemaphore(), DISPATCH_TIME_FOREVER);
			}
		}
	}
	
	FMetalGPUProfiler::ResetFrameBufferTimings();
}
#endif //PLATFORM_VISIONOS


/*=============================================================================
 *	The following RHI functions must be called from the main thread.
 *=============================================================================*/
FViewportRHIRef FMetalDynamicRHI::RHICreateViewport(void* WindowHandle, uint32 SizeX, uint32 SizeY, bool bIsFullscreen, EPixelFormat PreferredPixelFormat)
{
	check( IsInGameThread() );
    MTL_SCOPED_AUTORELEASE_POOL;
    
	// Use a default pixel format if none was specified
	if (PreferredPixelFormat == PF_Unknown)
	{
		static const auto* CVarDefaultBackBufferPixelFormat = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.DefaultBackBufferPixelFormat"));
		PreferredPixelFormat = EDefaultBackBufferPixelFormat::Convert2PixelFormat(EDefaultBackBufferPixelFormat::FromInt(CVarDefaultBackBufferPixelFormat->GetValueOnAnyThread()));
	}
	
	return new FMetalViewport(*Device, WindowHandle, SizeX, SizeY, bIsFullscreen, PreferredPixelFormat);
}

void FMetalDynamicRHI::RHIResizeViewport(FRHIViewport* Viewport, uint32 SizeX, uint32 SizeY, bool bIsFullscreen)
{
	RHIResizeViewport(Viewport, SizeX, SizeY, bIsFullscreen, PF_Unknown);
}

void FMetalDynamicRHI::RHIResizeViewport(FRHIViewport* ViewportRHI, uint32 SizeX, uint32 SizeY, bool bIsFullscreen, EPixelFormat PreferredPixelFormat)
{
    MTL_SCOPED_AUTORELEASE_POOL;
	check( IsInGameThread() );

	// Use a default pixel format if none was specified
	if (PreferredPixelFormat == PF_Unknown)
	{
		static const auto* CVarDefaultBackBufferPixelFormat = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.DefaultBackBufferPixelFormat"));
		PreferredPixelFormat = EDefaultBackBufferPixelFormat::Convert2PixelFormat(EDefaultBackBufferPixelFormat::FromInt(CVarDefaultBackBufferPixelFormat->GetValueOnAnyThread()));
	}
	
	FMetalViewport* Viewport = ResourceCast(ViewportRHI);
	Viewport->Resize(SizeX, SizeY, bIsFullscreen, PreferredPixelFormat);
}

void FMetalDynamicRHI::RHITick( float DeltaTime )
{
	check( IsInGameThread() );
}

/*=============================================================================
 *	Viewport functions.
 *=============================================================================*/

void FMetalRHICommandContext::RHIBeginDrawingViewport(FRHIViewport* ViewportRHI, FRHITexture* RenderTargetRHI)
{
    MTL_SCOPED_AUTORELEASE_POOL;
	FMetalViewport* Viewport = ResourceCast(ViewportRHI);
	check(Viewport);

	// Set the render target and viewport.
	if (RenderTargetRHI)
	{
		FRHIRenderTargetView RTV(RenderTargetRHI, GIsEditor ? ERenderTargetLoadAction::EClear : ERenderTargetLoadAction::ELoad);
		SetRenderTargets(1, &RTV, nullptr);
	}
	else
	{
		FRHIRenderTargetView RTV(Viewport->GetBackBuffer(EMetalViewportAccessRHI), GIsEditor ? ERenderTargetLoadAction::EClear : ERenderTargetLoadAction::ELoad);
		SetRenderTargets(1, &RTV, nullptr);
	}
}

void FMetalRHICommandContext::RHIEndDrawingViewport(FRHIViewport* ViewportRHI,bool bPresent,bool bLockToVsync)
{
    MTL_SCOPED_AUTORELEASE_POOL;
    
	FMetalViewport* Viewport = ResourceCast(ViewportRHI);

	// enqueue a present if desired
	static bool const bOffscreenOnly = FParse::Param(FCommandLine::Get(), TEXT("MetalOffscreenOnly"));
	if (bPresent && !bOffscreenOnly)
	{
		bool bNeedNativePresent = true;
#if PLATFORM_MAC || PLATFORM_VISIONOS
		// Handle custom present
		FRHICustomPresent* const CustomPresent = Viewport->GetCustomPresent();
		if (CustomPresent != nullptr)
		{
			int32 SyncInterval = 0;
			{
				SCOPE_CYCLE_COUNTER(STAT_MetalCustomPresentTime);
                SetCustomPresentViewport(Viewport);
                bNeedNativePresent = CustomPresent->Present(*this, SyncInterval);
                SetCustomPresentViewport(nullptr);
			}
			
			FMetalCommandBuffer* CurrentCommandBuffer = CurrentEncoder.GetCommandBuffer();
			check(CurrentCommandBuffer && CurrentCommandBuffer->GetMTLCmdBuffer());
			
			MTL::HandlerFunction Handler = [CustomPresent](MTL::CommandBuffer*) {
				CustomPresent->PostPresent();
			};
			
			CurrentCommandBuffer->GetMTLCmdBuffer()->addScheduledHandler(Handler);
		}
#endif
		
		if (bNeedNativePresent)
		{
			Viewport->Present(CommandQueue, bLockToVsync);
		}
	}
	
	Device.EndDrawingViewport(bPresent);
	
	Viewport->ReleaseDrawable();
}

FTextureRHIRef FMetalDynamicRHI::RHIGetViewportBackBuffer(FRHIViewport* ViewportRHI)
{
    MTL_SCOPED_AUTORELEASE_POOL;
    
	FMetalViewport* Viewport = ResourceCast(ViewportRHI);
	return FTextureRHIRef(Viewport->GetBackBuffer(EMetalViewportAccessRenderer).GetReference());
}

void FMetalDynamicRHI::RHIAdvanceFrameForGetViewportBackBuffer(FRHIViewport* ViewportRHI)
{
	if (GMetalSeparatePresentThread && (GRHISupportsRHIThread && IsRunningRHIInSeparateThread()))
	{
		FScopeLock Lock(&ViewportsMutex);
		for (FMetalViewport* Viewport : Viewports)
		{
			Viewport->Swap();
		}
	}
}
