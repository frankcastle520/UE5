// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VulkanContext.h: Class to generate Vulkan command buffers from RHI CommandLists
=============================================================================*/

#pragma once 

#include "VulkanResources.h"
#include "VulkanRHIPrivate.h"
#include "VulkanGPUProfiler.h"

class FVulkanCommandBufferManager;
class FVulkanDevice;
class FVulkanDynamicRHI;
class FVulkanOcclusionQueryPool;
class FVulkanPendingGfxState;
class FVulkanPendingComputeState;
class FVulkanQueue;
class FVulkanSwapChain;


class FVulkanCommandListContext : public IRHICommandContext
{
public:
	FVulkanCommandListContext(FVulkanDynamicRHI* InRHI, FVulkanDevice* InDevice, FVulkanQueue* InQueue, FVulkanCommandListContext* InImmediate);
	virtual ~FVulkanCommandListContext();

	static inline FVulkanCommandListContext& GetVulkanContext(IRHICommandContext& CmdContext)
	{
		return static_cast<FVulkanCommandListContext&>(CmdContext.GetLowestLevelContext());
	}

	inline bool IsImmediate() const
	{
		return Immediate == nullptr;
	}

	template <class ShaderType> void SetResourcesFromTables(const ShaderType* RESTRICT);
	void CommitGraphicsResourceTables();
	void CommitComputeResourceTables();

	virtual void RHISetStreamSource(uint32 StreamIndex, FRHIBuffer* VertexBuffer, uint32 Offset) final override;
	virtual void RHISetViewport(float MinX, float MinY, float MinZ, float MaxX, float MaxY, float MaxZ) final override;
	virtual void RHISetStereoViewport(float LeftMinX, float RightMinX, float LeftMinY, float RightMinY, float MinZ, float LeftMaxX, float RightMaxX, float LeftMaxY, float RightMaxY, float MaxZ) override;
	virtual void RHISetScissorRect(bool bEnable, uint32 MinX, uint32 MinY, uint32 MaxX, uint32 MaxY) final override;
	virtual void RHISetGraphicsPipelineState(FRHIGraphicsPipelineState* GraphicsState, uint32 StencilRef, bool bApplyAdditionalState) final override;
	void RHISetShaderTexture(FRHIGraphicsShader* Shader, uint32 TextureIndex, FRHITexture* NewTexture);
	void RHISetShaderTexture(FRHIComputeShader* PixelShader, uint32 TextureIndex, FRHITexture* NewTexture);
	void RHISetShaderSampler(FRHIComputeShader* ComputeShader, uint32 SamplerIndex, FRHISamplerState* NewState);
	void RHISetShaderSampler(FRHIGraphicsShader* Shader, uint32 SamplerIndex, FRHISamplerState* NewState);
	void RHISetUAVParameter(FRHIPixelShader* PixelShader, uint32 UAVIndex, FRHIUnorderedAccessView* UAV);
	void RHISetUAVParameter(FRHIComputeShader* ComputeShader, uint32 UAVIndex, FRHIUnorderedAccessView* UAV);
	void RHISetUAVParameter(FRHIComputeShader* ComputeShader, uint32 UAVIndex, FRHIUnorderedAccessView* UAV, uint32 InitialCount);
	void RHISetShaderResourceViewParameter(FRHIGraphicsShader* Shader, uint32 SamplerIndex, FRHIShaderResourceView* SRV);
	void RHISetShaderResourceViewParameter(FRHIComputeShader* ComputeShader, uint32 SamplerIndex, FRHIShaderResourceView* SRV);
	virtual void RHISetStaticUniformBuffers(const FUniformBufferStaticBindings& InUniformBuffers) final override;
	virtual void RHISetStaticUniformBuffer(FUniformBufferStaticSlot Slot, FRHIUniformBuffer* Buffer) final override;
	virtual void RHISetUniformBufferDynamicOffset(FUniformBufferStaticSlot Slot, uint32 InOffset) final override;
	void RHISetShaderUniformBuffer(FRHIGraphicsShader* Shader, uint32 BufferIndex, FRHIUniformBuffer* Buffer);
	void RHISetShaderUniformBuffer(FRHIComputeShader* ComputeShader, uint32 BufferIndex, FRHIUniformBuffer* Buffer);
	void RHISetShaderParameter(FRHIGraphicsShader* Shader, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue);
	void RHISetShaderParameter(FRHIComputeShader* ComputeShader, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue);
	virtual void RHISetShaderParameters(FRHIGraphicsShader* Shader, TConstArrayView<uint8> InParametersData, TConstArrayView<FRHIShaderParameter> InParameters, TConstArrayView<FRHIShaderParameterResource> InResourceParameters, TConstArrayView<FRHIShaderParameterResource> InBindlessParameters) final override;
	virtual void RHISetShaderParameters(FRHIComputeShader* Shader, TConstArrayView<uint8> InParametersData, TConstArrayView<FRHIShaderParameter> InParameters, TConstArrayView<FRHIShaderParameterResource> InResourceParameters, TConstArrayView<FRHIShaderParameterResource> InBindlessParameters) final override;
	virtual void RHISetStencilRef(uint32 StencilRef) final override;
	virtual void RHIDrawPrimitive(uint32 BaseVertexIndex, uint32 NumPrimitives, uint32 NumInstances) final override;
	virtual void RHIDrawPrimitiveIndirect(FRHIBuffer* ArgumentBuffer, uint32 ArgumentOffset) final override;
	virtual void RHIDrawIndexedIndirect(FRHIBuffer* IndexBufferRHI, FRHIBuffer* ArgumentsBufferRHI, int32 DrawArgumentsIndex, uint32 NumInstances) final override;
	virtual void RHIDrawIndexedPrimitive(FRHIBuffer* IndexBuffer, int32 BaseVertexIndex, uint32 FirstInstance, uint32 NumVertices, uint32 StartIndex, uint32 NumPrimitives, uint32 NumInstances) final override;
	virtual void RHIDrawIndexedPrimitiveIndirect(FRHIBuffer* IndexBuffer, FRHIBuffer* ArgumentBuffer, uint32 ArgumentOffset) final override;
#if PLATFORM_SUPPORTS_MESH_SHADERS
	virtual void RHIDispatchMeshShader(uint32 ThreadGroupCountX, uint32 ThreadGroupCountY, uint32 ThreadGroupCountZ) final override;
	virtual void RHIDispatchIndirectMeshShader(FRHIBuffer* ArgumentBuffer, uint32 ArgumentOffset) final override;
#endif
	virtual void RHISetDepthBounds(float MinDepth, float MaxDepth) final override;
#if WITH_RHI_BREADCRUMBS
	virtual void RHIBeginBreadcrumbGPU(FRHIBreadcrumbNode* Breadcrumb) final override;
	virtual void RHIEndBreadcrumbGPU  (FRHIBreadcrumbNode* Breadcrumb) final override;
#endif

	virtual void RHISetComputePipelineState(FRHIComputePipelineState* ComputePipelineState) final override;
	virtual void RHIDispatchComputeShader(uint32 ThreadGroupCountX, uint32 ThreadGroupCountY, uint32 ThreadGroupCountZ) final override;
	virtual void RHIDispatchIndirectComputeShader(FRHIBuffer* ArgumentBuffer, uint32 ArgumentOffset) final override;

	virtual void RHISetMultipleViewports(uint32 Count, const FViewportBounds* Data) final override;
	virtual void RHIClearUAVFloat(FRHIUnorderedAccessView* UnorderedAccessViewRHI, const FVector4f& Values) final override;
	virtual void RHIClearUAVUint(FRHIUnorderedAccessView* UnorderedAccessViewRHI, const FUintVector4& Values) final override;
	virtual void RHICopyTexture(FRHITexture* SourceTexture, FRHITexture* DestTexture, const FRHICopyTextureInfo& CopyInfo) final override;
	virtual void RHICopyBufferRegion(FRHIBuffer* DstBuffer, uint64 DstOffset, FRHIBuffer* SrcBuffer, uint64 SrcOffset, uint64 NumBytes) final override;
	virtual void RHIBeginTransitions(TArrayView<const FRHITransition*> Transitions) override final;
	virtual void RHIEndTransitions(TArrayView<const FRHITransition*> Transitions) override final;
	virtual void RHICopyToStagingBuffer(FRHIBuffer* SourceBuffer, FRHIStagingBuffer* DestinationStagingBuffer, uint32 Offset, uint32 NumBytes) final override;
	virtual void RHIWriteGPUFence(FRHIGPUFence* Fence) final override;

	// Render time measurement
	virtual void RHIBeginRenderQuery(FRHIRenderQuery* RenderQuery) final override;
	virtual void RHIEndRenderQuery(FRHIRenderQuery* RenderQuery) final override;

#if (RHI_NEW_GPU_PROFILER == 0)
	virtual void RHICalibrateTimers(FRHITimestampCalibrationQuery* CalibrationQuery) final override;
#endif

	virtual void RHIBeginDrawingViewport(FRHIViewport* Viewport, FRHITexture* RenderTargetRHI) final override;
	virtual void RHIEndDrawingViewport(FRHIViewport* Viewport, bool bPresent, bool bLockToVsync) final override;

	virtual void RHIBeginRenderPass(const FRHIRenderPassInfo& InInfo, const TCHAR* InName) final override;
	virtual void RHIEndRenderPass() final override;
	virtual void RHINextSubpass() final override;

	virtual void RHIClearRayTracingBindings(FRHIRayTracingScene* Scene) final override;
	virtual void RHICommitRayTracingBindings(FRHIRayTracingScene* Scene) final override;
	virtual void RHIClearShaderBindingTable(FRHIShaderBindingTable* SBT) final override;
	virtual void RHICommitShaderBindingTable(FRHIShaderBindingTable* SBT) final override;
	virtual void RHIBindAccelerationStructureMemory(FRHIRayTracingScene* Scene, FRHIBuffer* Buffer, uint32 BufferOffset) final override;
	virtual void RHIBuildAccelerationStructures(const TArrayView<const FRayTracingGeometryBuildParams> Params, const FRHIBufferRange& ScratchBufferRange) final override;
	virtual void RHIBuildAccelerationStructure(const FRayTracingSceneBuildParams& SceneBuildParams) final override;

	virtual void RHIRayTraceDispatch(FRHIRayTracingPipelineState* RayTracingPipelineState, FRHIRayTracingShader* RayGenShader,
		FRHIShaderBindingTable* SBT, const FRayTracingShaderBindings& GlobalResourceBindings,
		uint32 Width, uint32 Height) final override;
	virtual void RHIRayTraceDispatchIndirect(FRHIRayTracingPipelineState* RayTracingPipelineState, FRHIRayTracingShader* RayGenShader,
		FRHIShaderBindingTable* SBT, const FRayTracingShaderBindings& GlobalResourceBindings,
		FRHIBuffer* ArgumentBuffer, uint32 ArgumentOffset) final override;

	virtual void RHISetBindingsOnShaderBindingTable(FRHIShaderBindingTable* SBT,
		FRHIRayTracingPipelineState* Pipeline,
		uint32 NumBindings, const FRayTracingLocalShaderBindings* Bindings,
		ERayTracingBindingType BindingType) final override;

	inline FVulkanCommandBufferManager* GetCommandBufferManager()
	{
		return CommandBufferManager;
	}

	inline VulkanRHI::FTempBlockAllocator& GetTempBlockAllocator()
	{
		return *TempBlockAllocator;
	}

	inline FVulkanPendingGfxState* GetPendingGfxState()
	{
		return PendingGfxState;
	}

	inline FVulkanPendingComputeState* GetPendingComputeState()
	{
		return PendingComputeState;
	}

	inline void NotifyDeletedRenderTarget(VkImage Image)
	{
		if (CurrentFramebuffer && CurrentFramebuffer->ContainsRenderTarget(Image))
		{
			CurrentFramebuffer = nullptr;
		}
	}

	inline void NotifyDeletedImage(VkImage Image)
	{
		CommandBufferManager->NotifyDeletedImage(Image);
		Queue->NotifyDeletedImage(Image);
	}

	inline FVulkanRenderPass* GetCurrentRenderPass()
	{
		return CurrentRenderPass;
	}

	inline FVulkanFramebuffer* GetCurrentFramebuffer()
	{
		return CurrentFramebuffer;
	}

	inline uint64 GetFrameCounter() const
	{
		return FrameCounter;
	}

	inline FVulkanQueue* GetQueue()
	{
		return Queue;
	}

	void WriteBeginTimestamp(FVulkanCmdBuffer* CmdBuffer);
	void WriteEndTimestamp(FVulkanCmdBuffer* CmdBuffer);

#if (RHI_NEW_GPU_PROFILER == 0)
	void ReadAndCalculateGPUFrameTime();
	
	inline FVulkanGPUProfiler& GetGPUProfiler()
	{
		return GpuProfiler;
	}
#endif

	inline FVulkanDevice* GetDevice() const
	{
		return Device;
	}

	void BeginRecursiveCommand()
	{
		// Nothing to do
	}

	void EndRenderQueryInternal(FVulkanCmdBuffer* CmdBuffer, FVulkanRenderQuery* Query);

	void ReleasePendingState();

protected:
	FVulkanDynamicRHI* RHI;
	FVulkanCommandListContext* Immediate;
	FVulkanDevice* Device;
	FVulkanQueue* Queue;
	bool bSubmitAtNextSafePoint;
	bool bUniformBufferUploadRenderPassDirty = true;

	void BeginOcclusionQueryBatch(FVulkanCmdBuffer* CmdBuffer, uint32 NumQueriesInBatch);
	void EndOcclusionQueryBatch(FVulkanCmdBuffer* CmdBuffer);

	VulkanRHI::FTempBlockAllocator* TempBlockAllocator = nullptr;

	TArray<FString> EventStack;

	FVulkanCommandBufferManager* CommandBufferManager;

	FVulkanRenderPass* CurrentRenderPass = nullptr;
	FVulkanFramebuffer* CurrentFramebuffer = nullptr;

	FVulkanOcclusionQueryPool* CurrentOcclusionQueryPool = nullptr;

	FVulkanPendingGfxState* PendingGfxState;
	FVulkanPendingComputeState* PendingComputeState;

	// Match the D3D12 maximum of 16 constant buffers per shader stage.
	enum { MAX_UNIFORM_BUFFERS_PER_SHADER_STAGE = 16 };

	// Track the currently bound uniform buffers.
	FVulkanUniformBuffer* BoundUniformBuffers[SF_NumStandardFrequencies][MAX_UNIFORM_BUFFERS_PER_SHADER_STAGE] = {};

	// Bit array to track which uniform buffers have changed since the last draw call.
	uint16 DirtyUniformBuffers[SF_NumStandardFrequencies] = {};

	void PrepareForCPURead();
	void RequestSubmitCurrentCommands();

	void InternalClearMRT(FVulkanCmdBuffer* CmdBuffer, bool bClearColor, int32 NumClearColors, const FLinearColor* ColorArray, bool bClearDepth, float Depth, bool bClearStencil, uint32 Stencil);

public:
	bool IsSwapchainImage(FRHITexture* InTexture) const;
	VkFormat GetSwapchainImageFormat() const;
	FVulkanSwapChain* GetSwapChain() const;

	FVulkanRenderPass* PrepareRenderPassForPSOCreation(const FGraphicsPipelineStateInitializer& Initializer);
	FVulkanRenderPass* PrepareRenderPassForPSOCreation(const FVulkanRenderTargetLayout& Initializer);

private:
	void RHIClearMRT(bool bClearColor, int32 NumClearColors, const FLinearColor* ColorArray, bool bClearDepth, float Depth, bool bClearStencil, uint32 Stencil);

	inline bool SafePointSubmit()
	{
		if (bSubmitAtNextSafePoint)
		{
			InternalSubmitActiveCmdBuffer();
			bSubmitAtNextSafePoint = false;
			return true;
		}

		return false;
	}

	void InternalSubmitActiveCmdBuffer();

	friend class FVulkanDevice;
	friend class FVulkanDynamicRHI;

	// Number of times EndFrame() has been called on this context
	uint64 FrameCounter;

#if RHI_NEW_GPU_PROFILER
	void RegisterGPUWork(uint32 NumPrimitives = 0, uint32 NumVertices = 0)	{ checkNoEntry(); } // @todo - new gpu profiler
	void RegisterGPUDispatch(FIntVector GroupCount)	                        { checkNoEntry(); } // @todo - new gpu profiler
#else
	void RegisterGPUWork(uint32 NumPrimitives = 0, uint32 NumVertices = 0)	{ GpuProfiler.RegisterGPUWork(NumPrimitives, NumVertices); }
	void RegisterGPUDispatch(FIntVector GroupCount)	                        { GpuProfiler.RegisterGPUDispatch(GroupCount); }

	FVulkanGPUProfiler GpuProfiler;
	FVulkanGPUTiming* FrameTiming;
#endif

	template <typename TRHIShader>
	void ApplyStaticUniformBuffers(TRHIShader* Shader);

	TArray<FRHIUniformBuffer*> GlobalUniformBuffers;

	friend struct FVulkanCommandContextContainer;
};

class FVulkanCommandListContextImmediate : public FVulkanCommandListContext
{
public:
	FVulkanCommandListContextImmediate(FVulkanDynamicRHI* InRHI, FVulkanDevice* InDevice, FVulkanQueue* InQueue);
};

#if 0 // @todo: RHI command list refactor - todo
struct FVulkanCommandContextContainer : public IRHICommandContextContainer, public VulkanRHI::FDeviceChild
{
	FVulkanCommandListContext* CmdContext;

	FVulkanCommandContextContainer(FVulkanDevice* InDevice);

	virtual IRHICommandContext* GetContext() override final;
	virtual void FinishContext() override final;
	virtual void SubmitAndFreeContextContainer(int32 Index, int32 Num) override final;

	/** Custom new/delete with recycling */
	void* operator new(size_t Size);
	void operator delete(void* RawMemory);

private:
	friend class FVulkanDevice;
};
#endif

struct FVulkanContextArray : public TRHIPipelineArray<FVulkanCommandListContext*>
{
	FVulkanContextArray(FRHIContextArray const& Contexts)
		: TRHIPipelineArray(InPlace, nullptr)
	{
		for (ERHIPipeline Pipeline : MakeFlagsRange(ERHIPipeline::All))
		{
			IRHIComputeContext* Context = Contexts[Pipeline];
			(*this)[Pipeline] = Context ? static_cast<FVulkanCommandListContext*>(&Context->GetLowestLevelContext()) : nullptr;
		}
	}
};
