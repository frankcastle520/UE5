// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	D3D12Viewport.h: D3D viewport RHI definitions.
=============================================================================*/

#pragma once

#include "D3D12RHICommon.h"
#include "HAL/Runnable.h"
#include "MultiGPU.h"
#include "RHIResources.h"
#include "Templates/RefCounting.h"
#include "DXGIUtilities.h"

class FD3D12Texture;
class FD3D12UnorderedAccessView_RHI;

class FD3D12SyncPoint;
using FD3D12SyncPointRef = TRefCountPtr<FD3D12SyncPoint>;

class FD3D12Viewport : public FRHIViewport, public FD3D12AdapterChild
{
public:

	// Lock viewport windows association and back buffer destruction because of possible crash inside DXGI factory during a call to MakeWindowAssociation
	// Backbuffer release will wait on the call to MakeWindowAssociation while this will fail internally with 'The requested operation is not implemented.' in KernelBase.dll
	// Reported & known problem in DXGI and will be fixed with future release but DXGI is not part of the Agility SDK so a code side fix is needed for now.
	static FCriticalSection DXGIBackBufferLock;

	FD3D12Viewport(class FD3D12Adapter* InParent, HWND InWindowHandle, uint32 InSizeX, uint32 InSizeY, bool bInIsFullscreen, EPixelFormat InPixelFormat);

	void Init();

	~FD3D12Viewport();

	void Resize(uint32 InSizeX, uint32 InSizeY, bool bInIsFullscreen, EPixelFormat PreferredPixelFormat);

	/**
	 * If the swap chain has been invalidated by DXGI, resets the swap chain to the expected state; otherwise, does nothing.
	 * Called once/frame by the game thread on all viewports.
	 * @param bIgnoreFocus - Whether the reset should happen regardless of whether the window is focused.
	 */
	void ConditionalResetSwapChain(bool bIgnoreFocus);

	/** Presents the swap chain.
	 * Returns true if Present was done by Engine.
	 */
	bool Present(class FD3D12CommandContextBase& Context, bool bLockToVsync);

	// Accessors.
	FIntPoint GetSizeXY() const
	{
		return FIntPoint(SizeX, SizeY);
	}

	FD3D12Texture* GetBackBuffer_RenderThread() const
	{
		check(IsInRenderingThread());
#if D3D12RHI_USE_DUMMY_BACKBUFFER
		return DummyBackBuffer_RenderThread;
#else
		checkSlow(CurrentBackBuffer_RenderThread);
		return CurrentBackBuffer_RenderThread->Texture;
#endif
	}

#if D3D12RHI_SUPPORTS_UAV_BACKBUFFER
	FD3D12UnorderedAccessView_RHI* GetBackBufferUAV_RenderThread() const
	{
		checkSlow(CurrentBackBuffer_RenderThread);
		return CurrentBackBuffer_RenderThread->UAV;
	}
#endif

	FD3D12Texture* GetBackBuffer_RHIThread() const
	{
		checkSlow(CurrentBackBuffer_RHIThread);
		return CurrentBackBuffer_RHIThread->Texture;
	}

	FD3D12Texture* GetSDRBackBuffer_RHIThread() const
	{
		checkSlow(CurrentBackBuffer_RHIThread);

#if D3D12RHI_USE_SDR_BACKBUFFER
		if (PixelFormat != SDRPixelFormat)
		{
			return CurrentBackBuffer_RHIThread->TextureSDR;
		}
#endif

		return CurrentBackBuffer_RHIThread->Texture;
	}

#if WITH_MGPU
	uint32 GetNextPresentGPUIndex() const
	{
		FScopeLock Lock(&ExpectedBackBufferIndexLock);
		return BackBuffers[ExpectedBackBufferIndex_RenderThread].GPUIndex;
	}
#endif // WITH_MGPU

	virtual void WaitForFrameEventCompletion() override;
	virtual void IssueFrameEvent() override;

#if D3D12_VIEWPORT_EXPOSES_SWAP_CHAIN
	virtual void* GetNativeSwapChain() const override;
#endif // #if D3D12_VIEWPORT_EXPOSES_SWAP_CHAIN

	virtual void* GetNativeBackBufferTexture() const override;
	virtual void* GetNativeBackBufferRT() const override;

	virtual void SetCustomPresent(FRHICustomPresent* InCustomPresent) override;
	virtual FRHICustomPresent* GetCustomPresent() const override;

	virtual void* GetNativeWindow(void** AddParam = nullptr) const override { return (void*)WindowHandle; }

	uint32 GetNumBackBuffers() const { return NumBackBuffers; }

	inline const bool IsFullscreen() const { return bIsFullscreen; }

	/** Query the swap chain's current connected output for HDR support. */
	bool CurrentOutputSupportsHDR() const;

	/** Advance and get the next present GPU index */
	void AdvanceExpectedBackBufferIndex_RenderThread();

	void OnResumeRendering();
	void OnSuspendRendering();

private:
	bool IsPresentAllowed();

#if D3D12RHI_USE_DUMMY_BACKBUFFER
	FD3D12Texture* CreateDummyBackBufferTextures(FD3D12Adapter* InAdapter, EPixelFormat InPixelFormat, uint32 InSizeX, uint32 InSizeY);
#endif

	/**
	 * Presents the swap chain checking the return result.
	 * Returns true if Present was done by Engine.
	 */
	bool PresentChecked(IRHICommandContext& RHICmdContext, int32 SyncInterval);

	/**
	 * Presents the backbuffer to the viewport window.
	 * Returns the HRESULT for the call.
	 */
	HRESULT PresentInternal(int32 SyncInterval);

	void ResizeInternal();
	void FinalDestroyInternal();
	void ClearPresentQueue();

	// Determine how deep the swapchain should be
	void InitializeBackBufferArrays();

	/** See if HDR can be enabled or not based on RHI support and current engine settings. */
	bool CheckHDRSupport();

	/** Enable HDR meta data transmission and set the necessary color space. */
	void EnableHDR();

	/** Disable HDR meta data transmission and set the necessary color space. */
	void ShutdownHDR();

#if D3D12RHI_USE_DXGI_COLOR_SPACE
	/** Ensure the correct color space is set on the swap chain */
	void EnsureColorSpace(EDisplayColorGamut DisplayGamut, EDisplayOutputFormat OutputDevice);
#endif

	void SetBackBufferIndex_RHIThread(uint32 Index)
	{
		CurrentBackBufferIndex_RHIThread = Index % NumBackBuffers;
		CurrentBackBuffer_RHIThread = &BackBuffers[CurrentBackBufferIndex_RHIThread];
	}

	void SetBackBufferIndex_RenderThread(uint32 Index)
	{
		ExpectedBackBufferIndex_RenderThread = Index % NumBackBuffers;
		CurrentBackBuffer_RenderThread = &BackBuffers[ExpectedBackBufferIndex_RenderThread];
	}

private:
	const HWND WindowHandle;
	uint32 SizeX = 0;
	uint32 SizeY = 0;
	EPixelFormat PixelFormat;
#if D3D12RHI_USE_SDR_BACKBUFFER
	static constexpr EPixelFormat SDRPixelFormat = PF_B8G8R8A8;
#endif

	bool bIsFullscreen = false;
	bool bFullscreenLost = false;
	bool bIsValid = true;
	bool bAllowTearing = true;
	bool bNeedSwapChain = false;

#if D3D12_VIEWPORT_EXPOSES_SWAP_CHAIN
	TRefCountPtr<IDXGISwapChain1> SwapChain1;
#if DXGI_MAX_SWAPCHAIN_INTERFACE >= 2
	TRefCountPtr<IDXGISwapChain2> SwapChain2;
#endif
#if DXGI_MAX_SWAPCHAIN_INTERFACE >= 3
	TRefCountPtr<IDXGISwapChain3> SwapChain3;
#endif
#if DXGI_MAX_SWAPCHAIN_INTERFACE >= 4
	TRefCountPtr<IDXGISwapChain4> SwapChain4;
#endif

#if D3D12RHI_USE_DXGI_COLOR_SPACE
	DXGI_COLOR_SPACE_TYPE ColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
#endif
#endif // D3D12_VIEWPORT_EXPOSES_SWAP_CHAIN

	struct FBackBufferData
	{
		TRefCountPtr<FD3D12Texture>                 Texture;
#if D3D12RHI_USE_SDR_BACKBUFFER
		// When HDR is enabled, SDR backbuffers may be required on some architectures for game DVR or broadcasting
		TRefCountPtr<FD3D12Texture>                 TextureSDR;
#endif
#if D3D12RHI_SUPPORTS_UAV_BACKBUFFER
		TRefCountPtr<FD3D12UnorderedAccessView_RHI> UAV;
#endif
#if WITH_MGPU
		uint32                                      GPUIndex = 0;
#endif
	};

	static constexpr uint32 NumBackBuffers = GD3D12RHINumBackBuffers;
	TStaticArray<FBackBufferData, NumBackBuffers> BackBuffers;

#if D3D12RHI_USE_DUMMY_BACKBUFFER
	// Dummy back buffer texture which always references the current back buffer on the RHI thread
	TRefCountPtr<FD3D12Texture> DummyBackBuffer_RenderThread;
#endif

	FBackBufferData* CurrentBackBuffer_RHIThread = nullptr;
	FBackBufferData* CurrentBackBuffer_RenderThread = nullptr;

	uint32 CurrentBackBufferIndex_RHIThread = 0;
	uint32 ExpectedBackBufferIndex_RenderThread = 0;
	EDisplayColorGamut DisplayColorGamut = EDisplayColorGamut::sRGB_D65;
	EDisplayOutputFormat DisplayOutputFormat = EDisplayOutputFormat::SDR_sRGB;

	/** A fence value used to track the GPU's progress. */
	TArray<FD3D12SyncPointRef> FrameSyncPoints;

	FCustomPresentRHIRef CustomPresent;

#if WITH_MGPU
	// Where INDEX_NONE cycles through the GPU, otherwise the GPU index.
	int32 BackbufferMultiGPUBinding = 0;

	// Can very rarely be modified on the RHI thread as well if present is skipped
	mutable FCriticalSection ExpectedBackBufferIndexLock;
#endif
};

template<>
struct TD3D12ResourceTraits<FRHIViewport>
{
	typedef FD3D12Viewport TConcreteType;
};
