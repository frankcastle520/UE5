// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VulkanStatePipeline.cpp: Vulkan pipeline state implementation.
=============================================================================*/

#include "VulkanRHIPrivate.h"
#include "VulkanPipelineState.h"
#include "VulkanResources.h"
#include "VulkanPipeline.h"
#include "VulkanContext.h"
#include "VulkanPendingState.h"
#include "VulkanPipeline.h"
#include "VulkanLLM.h"
#include "RHICoreShader.h"

enum
{
	NumAllocationsPerPool = 8,
};

#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
static TAutoConsoleVariable<int32> GAlwaysWriteDS(
	TEXT("r.Vulkan.AlwaysWriteDS"),
	0,
	TEXT(""),
	ECVF_RenderThreadSafe
);
#endif

static bool ShouldAlwaysWriteDescriptors()
{
#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
	return (GAlwaysWriteDS.GetValueOnAnyThread() != 0);
#else
	return false;
#endif
}

FVulkanComputePipelineDescriptorState::FVulkanComputePipelineDescriptorState(FVulkanDevice* InDevice, FVulkanComputePipeline* InComputePipeline)
	: FVulkanCommonPipelineDescriptorState(InDevice, ShaderStage::NumComputeStages)
	, PackedUniformBuffersMask(0)
	, PackedUniformBuffersDirty(0)
	, ComputePipeline(InComputePipeline)
{
	LLM_SCOPE_VULKAN(ELLMTagVulkan::VulkanShaders);
	check(InComputePipeline);
	const FVulkanShaderHeader& CodeHeader = InComputePipeline->GetShaderCodeHeader();
	PackedUniformBuffers.Init(CodeHeader, PackedUniformBuffersMask);

	DescriptorSetsLayout = &InComputePipeline->GetLayout().GetDescriptorSetsLayout();

	UsedSetsMask = (CodeHeader.Bindings.Num() > 0) ? 1 : 0;

	CreateDescriptorWriteInfos();
	InComputePipeline->AddRef();

	ensure(DSWriter.Num() == 0 || DSWriter.Num() == 1);
}

void FVulkanCommonPipelineDescriptorState::CreateDescriptorWriteInfos()
{
	check(DSWriteContainer.DescriptorWrites.Num() == 0);
	check(UsedSetsMask <= (uint32)(((uint32)1 << MaxNumSets) - 1));

	for (uint32 Set = 0; Set < MaxNumSets; ++Set)
	{
		const FVulkanDescriptorSetsLayoutInfo::FStageInfo& StageInfo = DescriptorSetsLayout->StageInfos[Set];
		if (!StageInfo.Types.Num())
		{
			continue;
		}
		
		if (UseVulkanDescriptorCache())
		{
			DSWriteContainer.HashableDescriptorInfo.AddZeroed(StageInfo.Types.Num() + 1); // Add 1 for the Layout
		}
		DSWriteContainer.DescriptorWrites.AddZeroed(StageInfo.Types.Num());
		DSWriteContainer.DescriptorImageInfo.AddZeroed(StageInfo.NumImageInfos);
		DSWriteContainer.DescriptorBufferInfo.AddZeroed(StageInfo.NumBufferInfos);
		DSWriteContainer.AccelerationStructureWrites.AddZeroed(StageInfo.NumAccelerationStructures);
		DSWriteContainer.AccelerationStructures.AddZeroed(StageInfo.NumAccelerationStructures);

		checkf(StageInfo.Types.Num() < 255, TEXT("Need more bits for BindingToDynamicOffsetMap (currently 8)! Requires %d descriptor bindings in a set!"), StageInfo.Types.Num());
		DSWriteContainer.BindingToDynamicOffsetMap.AddUninitialized(StageInfo.Types.Num());
	}

	FMemory::Memset(DSWriteContainer.BindingToDynamicOffsetMap.GetData(), 255, DSWriteContainer.BindingToDynamicOffsetMap.Num());

	check(DSWriter.Num() == 0);
	DSWriter.AddDefaulted(MaxNumSets);

	const FVulkanSamplerState& DefaultSampler = Device->GetDefaultSampler();
	const FVulkanView::FTextureView& DefaultImageView = Device->GetDefaultImageView();

	FVulkanHashableDescriptorInfo* CurrentHashableDescriptorInfo = nullptr;
	if (UseVulkanDescriptorCache())
	{
		CurrentHashableDescriptorInfo = DSWriteContainer.HashableDescriptorInfo.GetData();
	}
	VkWriteDescriptorSet* CurrentDescriptorWrite = DSWriteContainer.DescriptorWrites.GetData();
	VkDescriptorImageInfo* CurrentImageInfo = DSWriteContainer.DescriptorImageInfo.GetData();
	VkDescriptorBufferInfo* CurrentBufferInfo = DSWriteContainer.DescriptorBufferInfo.GetData();
	VkWriteDescriptorSetAccelerationStructureKHR* CurrentAccelerationStructuresWriteDescriptors = DSWriteContainer.AccelerationStructureWrites.GetData();
	VkAccelerationStructureKHR* CurrentAccelerationStructures = DSWriteContainer.AccelerationStructures.GetData();

	uint8* CurrentBindingToDynamicOffsetMap = DSWriteContainer.BindingToDynamicOffsetMap.GetData();
	TArray<uint32> DynamicOffsetsStart;
	DynamicOffsetsStart.AddZeroed(MaxNumSets);
	uint32 TotalNumDynamicOffsets = 0;

	for (uint32 Set = 0; Set < MaxNumSets; ++Set)
	{
		const FVulkanDescriptorSetsLayoutInfo::FStageInfo& StageInfo = DescriptorSetsLayout->StageInfos[Set];
		if (!StageInfo.Types.Num())
		{
			continue;
		}

		DynamicOffsetsStart[Set] = TotalNumDynamicOffsets;

		const uint32 NumDynamicOffsets = DSWriter[Set].SetupDescriptorWrites(
			StageInfo.Types, CurrentHashableDescriptorInfo,
			CurrentDescriptorWrite, CurrentImageInfo, CurrentBufferInfo, CurrentBindingToDynamicOffsetMap,
			CurrentAccelerationStructuresWriteDescriptors,
			CurrentAccelerationStructures,
			DefaultSampler, DefaultImageView);

		TotalNumDynamicOffsets += NumDynamicOffsets;

		if (CurrentHashableDescriptorInfo) // UseVulkanDescriptorCache()
		{
			CurrentHashableDescriptorInfo += StageInfo.Types.Num();
			CurrentHashableDescriptorInfo->Layout.Max0 = UINT32_MAX;
			CurrentHashableDescriptorInfo->Layout.Max1 = UINT32_MAX;
			CurrentHashableDescriptorInfo->Layout.LayoutId = DescriptorSetsLayout->GetHandleIds()[Set];
			++CurrentHashableDescriptorInfo;
		}

		CurrentDescriptorWrite += StageInfo.Types.Num();
		CurrentImageInfo += StageInfo.NumImageInfos;
		CurrentBufferInfo += StageInfo.NumBufferInfos;
		CurrentAccelerationStructuresWriteDescriptors += StageInfo.NumAccelerationStructures;
		CurrentAccelerationStructures += StageInfo.NumAccelerationStructures;

		CurrentBindingToDynamicOffsetMap += StageInfo.Types.Num();
	}

	DynamicOffsets.AddZeroed(TotalNumDynamicOffsets);
	for (uint32 Set = 0; Set < MaxNumSets; ++Set)
	{
		DSWriter[Set].DynamicOffsets = DynamicOffsetsStart[Set] + DynamicOffsets.GetData();
	}

	DescriptorSetHandles.AddZeroed(MaxNumSets);
}


static inline VulkanRHI::FVulkanAllocation UpdatePackedUniformBuffers(const FPackedUniformBuffers& PackedUniformBuffers, FVulkanCommandListContext* InContext, FVulkanCmdBuffer* InCmdBuffer)
{
	const FPackedUniformBuffers::FPackedBuffer& StagedUniformBuffer = PackedUniformBuffers.GetBuffer();

	const uint32 UBSize = (uint32)StagedUniformBuffer.Num();
	const uint32 UBAlign = (uint32)InCmdBuffer->GetDevice()->GetLimits().minUniformBufferOffsetAlignment;

	VulkanRHI::FVulkanAllocation TempAllocation;
	uint8* MappedPointer = InContext->GetTempBlockAllocator().Alloc(UBSize, UBAlign, InCmdBuffer, TempAllocation);

	FMemory::Memcpy(MappedPointer, StagedUniformBuffer.GetData(), UBSize);

	return TempAllocation;
}


template<bool bUseDynamicGlobalUBs>
bool FVulkanComputePipelineDescriptorState::InternalUpdateDescriptorSets(FVulkanCommandListContext* CmdListContext, FVulkanCmdBuffer* CmdBuffer)
{
#if VULKAN_ENABLE_AGGRESSIVE_STATS
	SCOPE_CYCLE_COUNTER(STAT_VulkanUpdateDescriptorSets);
#endif

	// Early exit
	if (!UsedSetsMask)
	{
		return false;
	}

	if (PackedUniformBuffersDirty != 0)
	{
#if VULKAN_ENABLE_AGGRESSIVE_STATS
		SCOPE_CYCLE_COUNTER(STAT_VulkanApplyPackedUniformBuffers);
#endif
		SubmitPackedUniformBuffers<bUseDynamicGlobalUBs>(DSWriter[ShaderStage::Compute], UpdatePackedUniformBuffers(PackedUniformBuffers, CmdListContext, CmdBuffer));
		PackedUniformBuffersDirty = 0;
	}

	// We are not using UseVulkanDescriptorCache() for compute pipelines
	// Compute tend to use volatile resources that polute descriptor cache

	if (!CmdBuffer->AcquirePoolSetAndDescriptorsIfNeeded(*DescriptorSetsLayout, true, DescriptorSetHandles.GetData()))
	{
		return false;
	}

	const VkDescriptorSet DescriptorSet = DescriptorSetHandles[0];
	DSWriter[0].SetDescriptorSet(DescriptorSet);
#if VULKAN_VALIDATE_DESCRIPTORS_WRITTEN
	for(FVulkanDescriptorSetWriter& Writer : DSWriter)
	{
		Writer.CheckAllWritten();
	}
#endif

	{
#if VULKAN_ENABLE_AGGRESSIVE_STATS
		INC_DWORD_STAT_BY(STAT_VulkanNumUpdateDescriptors, DSWriteContainer.DescriptorWrites.Num());
		INC_DWORD_STAT(STAT_VulkanNumDescSets);
		SCOPE_CYCLE_COUNTER(STAT_VulkanVkUpdateDS);
#endif
		VulkanRHI::vkUpdateDescriptorSets(Device->GetInstanceHandle(), DSWriteContainer.DescriptorWrites.Num(), DSWriteContainer.DescriptorWrites.GetData(), 0, nullptr);
	}

	return true;
}

void FVulkanComputePipelineDescriptorState::UpdateBindlessDescriptors(FVulkanCommandListContext* CmdListContext, FVulkanCmdBuffer* CmdBuffer)
{
	check(bUseBindless);

	// We should only have uniform buffers at this point
	check(DSWriteContainer.DescriptorBufferInfo.Num() == DSWriteContainer.DescriptorWrites.Num());
	check(DSWriteContainer.DescriptorImageInfo.Num() == 0);

	FVulkanBindlessDescriptorManager::FUniformBufferDescriptorArrays StageUBs;

	const FVulkanShaderHeader& Header = ComputePipeline->GetShaderCodeHeader();

	TArray<VkDescriptorAddressInfoEXT>& DescriptorAddressInfos = StageUBs[ShaderStage::EStage::Compute];
	DescriptorAddressInfos.SetNumZeroed(Header.NumBoundUniformBuffers);
	uint32 UBIndex = 0;

	// UBs are currently set from a fresh batch of descriptors for every call, so ignore PackedUniformBuffersDirty
	check(PackedUniformBuffersMask <= 1);
	if (PackedUniformBuffersMask != 0)
	{
		const FPackedUniformBuffers::FPackedBuffer& StagedUniformBuffer = PackedUniformBuffers.GetBuffer();
		const int32 UBSize = StagedUniformBuffer.Num();
		const int32 BindingIndex = 0;
		const VkDeviceSize UBOffsetAlignment = Device->GetLimits().minUniformBufferOffsetAlignment;

		VulkanRHI::FVulkanAllocation TempAllocation;
		uint8* MappedPointer = CmdListContext->GetTempBlockAllocator().Alloc(UBSize, UBOffsetAlignment, CmdBuffer, TempAllocation, &DescriptorAddressInfos[BindingIndex]);
		FMemory::Memcpy(MappedPointer, StagedUniformBuffer.GetData(), UBSize);

		PackedUniformBuffersDirty = 0;
		++UBIndex;
	}

	for (;UBIndex < Header.NumBoundUniformBuffers; ++UBIndex)
	{
		VkDescriptorAddressInfoEXT& DescriptorAddressInfo = DescriptorAddressInfos[UBIndex];
		check(DescriptorAddressInfo.sType == 0);

		VkWriteDescriptorSet& WriteDescriptorSet = DSWriter[ShaderStage::EStage::Compute].WriteDescriptors[UBIndex];
		check(WriteDescriptorSet.dstBinding == UBIndex);
		check(WriteDescriptorSet.dstArrayElement == 0);
		check(WriteDescriptorSet.descriptorCount == 1);
		check(WriteDescriptorSet.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
		checkSlow(WriteDescriptorSet.pBufferInfo);

		VkBufferDeviceAddressInfo BufferInfo;
		ZeroVulkanStruct(BufferInfo, VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO);
		BufferInfo.buffer = WriteDescriptorSet.pBufferInfo->buffer;
		VkDeviceAddress BufferAddress = VulkanRHI::vkGetBufferDeviceAddressKHR(Device->GetInstanceHandle(), &BufferInfo);

		DescriptorAddressInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT;
		DescriptorAddressInfo.address = BufferAddress + WriteDescriptorSet.pBufferInfo->offset;
		DescriptorAddressInfo.range = WriteDescriptorSet.pBufferInfo->range;
	}

	// Send to descriptor manager
	Device->GetBindlessDescriptorManager()->RegisterUniformBuffers(CmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, StageUBs);
}

FVulkanGraphicsPipelineDescriptorState::FVulkanGraphicsPipelineDescriptorState(FVulkanDevice* InDevice, FVulkanRHIGraphicsPipelineState* InGfxPipeline)
	: FVulkanCommonPipelineDescriptorState(InDevice, ShaderStage::NumGraphicsStages)
	, GfxPipeline(InGfxPipeline)
{
	LLM_SCOPE_VULKAN(ELLMTagVulkan::VulkanShaders);
	FMemory::Memzero(PackedUniformBuffersMask);
	FMemory::Memzero(PackedUniformBuffersDirty);

	check(InGfxPipeline && InGfxPipeline->Layout && InGfxPipeline->Layout->IsGfxLayout());
	DescriptorSetsLayout = &InGfxPipeline->Layout->GetDescriptorSetsLayout();

	UsedSetsMask = 0;

	const FVulkanShaderFactory& ShaderFactory = Device->GetShaderFactory();


	uint64 VertexShaderKey = InGfxPipeline->GetShaderKey(SF_Vertex);
	if (VertexShaderKey)
	{
		const FVulkanVertexShader* VertexShader = ShaderFactory.LookupShader<FVulkanVertexShader>(InGfxPipeline->GetShaderKey(SF_Vertex));
		check(VertexShader);

		PackedUniformBuffers[ShaderStage::Vertex].Init(VertexShader->GetCodeHeader(), PackedUniformBuffersMask[ShaderStage::Vertex]);
		UsedSetsMask |= VertexShader->GetCodeHeader().Bindings.Num() ? (1u << ShaderStage::Vertex) : 0u;
	}

	uint64 PixelShaderKey = InGfxPipeline->GetShaderKey(SF_Pixel);
	if (PixelShaderKey)
	{
		const FVulkanPixelShader* PixelShader = ShaderFactory.LookupShader<FVulkanPixelShader>(PixelShaderKey);
		check(PixelShader);

		PackedUniformBuffers[ShaderStage::Pixel].Init(PixelShader->GetCodeHeader(), PackedUniformBuffersMask[ShaderStage::Pixel]);
		UsedSetsMask |= PixelShader->GetCodeHeader().Bindings.Num() ? (1u << ShaderStage::Pixel) : 0u;
	}

#if PLATFORM_SUPPORTS_MESH_SHADERS
	uint64 MeshShaderKey = InGfxPipeline->GetShaderKey(SF_Mesh);
	if (MeshShaderKey)
	{
		const FVulkanMeshShader* MeshShader = ShaderFactory.LookupShader<FVulkanMeshShader>(MeshShaderKey);
		check(MeshShader);

		PackedUniformBuffers[ShaderStage::Mesh].Init(MeshShader->GetCodeHeader(), PackedUniformBuffersMask[ShaderStage::Mesh]);
		UsedSetsMask |= MeshShader->GetCodeHeader().Bindings.Num() ? (1u << ShaderStage::Mesh) : 0u;
	}

	uint64 TaskShaderKey = InGfxPipeline->GetShaderKey(SF_Amplification);
	if (TaskShaderKey)
	{
		const FVulkanTaskShader* TaskShader = ShaderFactory.LookupShader<FVulkanTaskShader>(TaskShaderKey);
		check(TaskShader);

		PackedUniformBuffers[ShaderStage::Task].Init(TaskShader->GetCodeHeader(), PackedUniformBuffersMask[ShaderStage::Task]);
		UsedSetsMask |= TaskShader->GetCodeHeader().Bindings.Num() ? (1u << ShaderStage::Task) : 0u;
	}
#endif

#if VULKAN_SUPPORTS_GEOMETRY_SHADERS
	uint64 GeometryShaderKey = InGfxPipeline->GetShaderKey(SF_Geometry);
	if (GeometryShaderKey)
	{
		const FVulkanGeometryShader* GeometryShader = ShaderFactory.LookupShader<FVulkanGeometryShader>(GeometryShaderKey);
		check(GeometryShader);

		PackedUniformBuffers[ShaderStage::Geometry].Init(GeometryShader->GetCodeHeader(), PackedUniformBuffersMask[ShaderStage::Geometry]);
		UsedSetsMask |= GeometryShader->GetCodeHeader().Bindings.Num() ? (1u << ShaderStage::Geometry) : 0u;
	}
#endif

	CreateDescriptorWriteInfos();

	//UE_LOG(LogVulkanRHI, Warning, TEXT("GfxPSOState %p For PSO %p Writes:%d"), this, InGfxPipeline, DSWriteContainer.DescriptorWrites.Num());

	InGfxPipeline->AddRef();
}

template<bool bUseDynamicGlobalUBs>
bool FVulkanGraphicsPipelineDescriptorState::InternalUpdateDescriptorSets(FVulkanCommandListContext* CmdListContext, FVulkanCmdBuffer* CmdBuffer)
{
#if VULKAN_ENABLE_AGGRESSIVE_STATS
	SCOPE_CYCLE_COUNTER(STAT_VulkanUpdateDescriptorSets);
#endif

	// Early exit
	if (!UsedSetsMask)
	{
		return false;
	}

	// Process updates
	{
#if VULKAN_ENABLE_AGGRESSIVE_STATS
		SCOPE_CYCLE_COUNTER(STAT_VulkanApplyPackedUniformBuffers);
#endif
		for (int32 Stage = 0; Stage < ShaderStage::NumGraphicsStages; ++Stage)
		{
			if (PackedUniformBuffersDirty[Stage] != 0)
			{
				MarkDirty(SubmitPackedUniformBuffers<bUseDynamicGlobalUBs>(DSWriter[Stage], UpdatePackedUniformBuffers(PackedUniformBuffers[Stage], CmdListContext, CmdBuffer)));
				PackedUniformBuffersDirty[Stage] = 0;
			}
		}
	}

	if (UseVulkanDescriptorCache() && !HasVolatileResources())
	{
		if (bIsResourcesDirty)
		{
			Device->GetDescriptorSetCache().GetDescriptorSets(GetDSetsKey(), *DescriptorSetsLayout, DSWriter, DescriptorSetHandles.GetData());
			bIsResourcesDirty = false;
		}
	}
	else
	{
		const bool bNeedsWrite = (bIsResourcesDirty || ShouldAlwaysWriteDescriptors());

		// Allocate sets based on what changed
		if (CmdBuffer->AcquirePoolSetAndDescriptorsIfNeeded(*DescriptorSetsLayout, bNeedsWrite, DescriptorSetHandles.GetData()))
		{
			uint32 RemainingSetsMask = UsedSetsMask;
			uint32 Set = 0;
			uint32 NumSets = 0;
			while (RemainingSetsMask)
			{
				if (RemainingSetsMask & 1)
				{
					const VkDescriptorSet DescriptorSet = DescriptorSetHandles[Set];
					DSWriter[Set].SetDescriptorSet(DescriptorSet);
#if VULKAN_VALIDATE_DESCRIPTORS_WRITTEN
					DSWriter[Set].CheckAllWritten();
#endif
					++NumSets;
				}

				++Set;
				RemainingSetsMask >>= 1;
			}

	#if VULKAN_ENABLE_AGGRESSIVE_STATS
			INC_DWORD_STAT_BY(STAT_VulkanNumUpdateDescriptors, DSWriteContainer.DescriptorWrites.Num());
			INC_DWORD_STAT_BY(STAT_VulkanNumDescSets, NumSets);
			SCOPE_CYCLE_COUNTER(STAT_VulkanVkUpdateDS);
	#endif
			VulkanRHI::vkUpdateDescriptorSets(Device->GetInstanceHandle(), DSWriteContainer.DescriptorWrites.Num(), DSWriteContainer.DescriptorWrites.GetData(), 0, nullptr);

			bIsResourcesDirty = false;
		}
	}

	return true;
}

void FVulkanGraphicsPipelineDescriptorState::UpdateBindlessDescriptors(FVulkanCommandListContext* CmdListContext, FVulkanCmdBuffer* CmdBuffer)
{
	check(bUseBindless);

	// We should only have uniform buffers at this point
	check(DSWriteContainer.DescriptorBufferInfo.Num() == DSWriteContainer.DescriptorWrites.Num());
	check(DSWriteContainer.DescriptorImageInfo.Num() == 0);

	const VkDeviceSize UBOffsetAlignment = Device->GetLimits().minUniformBufferOffsetAlignment;

	FVulkanBindlessDescriptorManager::FUniformBufferDescriptorArrays StageUBs;

	// Process updates
	{
#if VULKAN_ENABLE_AGGRESSIVE_STATS
		SCOPE_CYCLE_COUNTER(STAT_VulkanApplyPackedUniformBuffers);
#endif
		for (int32 Stage = 0; Stage < ShaderStage::NumGraphicsStages; ++Stage)
		{
			const FVulkanShader* VulkanShader = GfxPipeline->GetVulkanShader(GetFrequencyForGfxStage((ShaderStage::EStage)Stage));
			if (!VulkanShader)
			{
				continue;
			}

			const FVulkanShaderHeader& Header = VulkanShader->GetCodeHeader();

			TArray<VkDescriptorAddressInfoEXT>& DescriptorAddressInfos = StageUBs[Stage];
			DescriptorAddressInfos.SetNumZeroed(Header.NumBoundUniformBuffers);
			uint32 UBIndex = 0;

			// UBs are currently set from a fresh batch of descriptors for every call, so ignore PackedUniformBuffersDirty
			check(PackedUniformBuffersMask[Stage] <= 1);
			if (PackedUniformBuffersMask[Stage] != 0)
			{
				const FPackedUniformBuffers::FPackedBuffer& StagedUniformBuffer = PackedUniformBuffers[Stage].GetBuffer();
				const int32 UBSize = StagedUniformBuffer.Num();
				const int32 BindingIndex = 0;

				VulkanRHI::FVulkanAllocation TempAllocation;
				uint8* MappedPointer = CmdListContext->GetTempBlockAllocator().Alloc(UBSize, UBOffsetAlignment, CmdBuffer, TempAllocation, &DescriptorAddressInfos[BindingIndex]);
				FMemory::Memcpy(MappedPointer, StagedUniformBuffer.GetData(), UBSize);

				PackedUniformBuffersDirty[Stage] = 0;
				++UBIndex;
			}

			for (; UBIndex < Header.NumBoundUniformBuffers; ++UBIndex)
			{
				VkDescriptorAddressInfoEXT& DescriptorAddressInfo = DescriptorAddressInfos[UBIndex];
				check(DescriptorAddressInfo.sType == 0);

				VkWriteDescriptorSet& WriteDescriptorSet = DSWriter[Stage].WriteDescriptors[UBIndex];
				check(WriteDescriptorSet.dstBinding == UBIndex);
				check(WriteDescriptorSet.dstArrayElement == 0);
				check(WriteDescriptorSet.descriptorCount == 1);
				check(WriteDescriptorSet.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
				checkSlow(WriteDescriptorSet.pBufferInfo);

				VkBufferDeviceAddressInfo BufferInfo;
				ZeroVulkanStruct(BufferInfo, VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO);
				BufferInfo.buffer = WriteDescriptorSet.pBufferInfo->buffer;
				VkDeviceAddress BufferAddress = VulkanRHI::vkGetBufferDeviceAddressKHR(Device->GetInstanceHandle(), &BufferInfo);

				DescriptorAddressInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT;
				DescriptorAddressInfo.address = BufferAddress + WriteDescriptorSet.pBufferInfo->offset;
				DescriptorAddressInfo.range = WriteDescriptorSet.pBufferInfo->range;
			}
		}
	}

	// Send to descriptor manager
	Device->GetBindlessDescriptorManager()->RegisterUniformBuffers(CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, StageUBs);
}


template <typename TRHIShader>
void FVulkanCommandListContext::ApplyStaticUniformBuffers(TRHIShader* Shader)
{
	if (Shader)
	{
		const auto& StaticSlots = Shader->GetStaticSlots();
		const auto& UBInfos = Shader->GetCodeHeader().UniformBufferInfos;

		for (int32 BufferIndex = 0; BufferIndex < StaticSlots.Num(); ++BufferIndex)
		{
			const FUniformBufferStaticSlot Slot = StaticSlots[BufferIndex];

			if (IsUniformBufferStaticSlotValid(Slot))
			{
				FRHIUniformBuffer* Buffer = GlobalUniformBuffers[Slot];
				UE::RHICore::ValidateStaticUniformBuffer(Buffer, Slot, UBInfos[BufferIndex].LayoutHash);

				if (Buffer)
				{
					RHISetShaderUniformBuffer(Shader, BufferIndex, Buffer);
				}
			}
		}
	}
}

void FVulkanCommandListContext::RHISetGraphicsPipelineState(FRHIGraphicsPipelineState* GraphicsState, uint32 StencilRef, bool bApplyAdditionalState)
{
	FVulkanRHIGraphicsPipelineState* Pipeline = ResourceCast(GraphicsState);
	
	FVulkanPipelineStateCacheManager* PipelineStateCache = Device->GetPipelineStateCache();
	PipelineStateCache->LRUTouch(Pipeline);

	FVulkanCmdBuffer* CmdBuffer = CommandBufferManager->GetActiveCmdBuffer();
	bool bForceResetPipeline = !CmdBuffer->bHasPipeline;

	if (PendingGfxState->SetGfxPipeline(Pipeline, bForceResetPipeline))
	{
#if VULKAN_ENABLE_AGGRESSIVE_STATS
		SCOPE_CYCLE_COUNTER(STAT_VulkanPipelineBind);
#endif
		PendingGfxState->Bind(CmdBuffer->GetHandle());
		CmdBuffer->bHasPipeline = true;
		PendingGfxState->MarkNeedsDynamicStates();
	}

	PendingGfxState->SetStencilRef(StencilRef);

	if (bApplyAdditionalState)
	{
		ApplyStaticUniformBuffers(static_cast<FVulkanVertexShader*>(Pipeline->VulkanShaders[ShaderStage::Vertex]));
#if PLATFORM_SUPPORTS_MESH_SHADERS
		ApplyStaticUniformBuffers(static_cast<FVulkanMeshShader*>(Pipeline->VulkanShaders[ShaderStage::Mesh]));
		ApplyStaticUniformBuffers(static_cast<FVulkanTaskShader*>(Pipeline->VulkanShaders[ShaderStage::Task]));
#endif
#if PLATFORM_SUPPORTS_GEOMETRY_SHADERS
		ApplyStaticUniformBuffers(static_cast<FVulkanGeometryShader*>(Pipeline->VulkanShaders[ShaderStage::Geometry]));
#endif
		ApplyStaticUniformBuffers(static_cast<FVulkanPixelShader*>(Pipeline->VulkanShaders[ShaderStage::Pixel]));
	}
}

void FVulkanCommandListContext::RHISetComputePipelineState(FRHIComputePipelineState* ComputePipelineState)
{
	FVulkanCmdBuffer* CmdBuffer = CommandBufferManager->GetActiveCmdBuffer();
	if (CmdBuffer->IsInsideRenderPass())
	{
		if (GVulkanSubmitAfterEveryEndRenderPass)
		{
			CommandBufferManager->SubmitActiveCmdBuffer();
			CommandBufferManager->PrepareForNewActiveCommandBuffer();
			CmdBuffer = CommandBufferManager->GetActiveCmdBuffer();
		}
	}

	if (CmdBuffer->CurrentDescriptorPoolSetContainer == nullptr)
	{
		CmdBuffer->CurrentDescriptorPoolSetContainer = &Device->GetDescriptorPoolsManager().AcquirePoolSetContainer();
	}

	//#todo-rco: Set PendingGfx to null
	FVulkanComputePipeline* ComputePipeline = ResourceCast(ComputePipelineState);
	PendingComputeState->SetComputePipeline(ComputePipeline);

	ApplyStaticUniformBuffers(ResourceCast(ComputePipeline->GetComputeShader()));
}


template bool FVulkanGraphicsPipelineDescriptorState::InternalUpdateDescriptorSets<true>(FVulkanCommandListContext* CmdListContext, FVulkanCmdBuffer* CmdBuffer);
template bool FVulkanGraphicsPipelineDescriptorState::InternalUpdateDescriptorSets<false>(FVulkanCommandListContext* CmdListContext, FVulkanCmdBuffer* CmdBuffer);
template bool FVulkanComputePipelineDescriptorState::InternalUpdateDescriptorSets<true>(FVulkanCommandListContext* CmdListContext, FVulkanCmdBuffer* CmdBuffer);
template bool FVulkanComputePipelineDescriptorState::InternalUpdateDescriptorSets<false>(FVulkanCommandListContext* CmdListContext, FVulkanCmdBuffer* CmdBuffer);


void FVulkanDescriptorSetWriter::CheckAllWritten()
{
#if VULKAN_VALIDATE_DESCRIPTORS_WRITTEN
	auto GetVkDescriptorTypeString = [](VkDescriptorType Type)
	{
		switch (Type)
		{
			// + 19 to skip "VK_DESCRIPTOR_TYPE_"
	#define VKSWITCHCASE(x)	case x: return FString(&TEXT(#x)[19]);
			VKSWITCHCASE(VK_DESCRIPTOR_TYPE_SAMPLER)
				VKSWITCHCASE(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
				VKSWITCHCASE(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE)
				VKSWITCHCASE(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
				VKSWITCHCASE(VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER)
				VKSWITCHCASE(VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER)
				VKSWITCHCASE(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
				VKSWITCHCASE(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
				VKSWITCHCASE(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC)
				VKSWITCHCASE(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)
				VKSWITCHCASE(VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT)
	#undef VKSWITCHCASE
		default:
			break;
		}

		return FString::Printf(TEXT("Unknown VkDescriptorType %d"), (int32)Type);
	};

	const uint32 Writes = NumWrites;
	if (Writes == 0)
		return;

	bool bFail = false;
	if(Writes <= 32) //early out for the most common case.
	{
		bFail = WrittenMask[0] != ((1llu << Writes)-1);
	}
	else
	{
		const int32 Last = int32(WrittenMask.Num()-1);
		for(int32 i = 0; !bFail && i < Last; ++i)
		{
			uint64 Mask = WrittenMask[i];
			bFail = bFail || Mask != 0xffffffff; 
		}

		const uint32 TailCount = Writes - (Last * 32);
		check(TailCount != 0);
		const uint32 TailMask = (1llu << TailCount)-1;
		bFail = bFail || TailMask != WrittenMask[Last];
	}

	if(bFail)
	{
		FString Descriptors;
		for (uint32 i = 0; i < Writes; ++i)
		{
			uint32 Index = i / 32;
			uint32 Mask = i % 32;
			if(0 == (WrittenMask[Index] & (1llu << Mask)))
			{
				FString TypeString = GetVkDescriptorTypeString(WriteDescriptors[i].descriptorType);
				Descriptors += FString::Printf(TEXT("\t\tDescriptorWrite %d/%d Was not written(Type %s)\n"), i, NumWrites, *TypeString);
			}
		}
		UE_LOG(LogVulkanRHI, Warning, TEXT("Not All descriptors where filled out. this can/will cause a driver crash\n%s\n"), *Descriptors);
		ensureMsgf(false, TEXT("Not All descriptors where filled out. this can/will cause a driver crash\n%s\n"), *Descriptors);
	}
#endif
}

void FVulkanDescriptorSetWriter::Reset()
{
	bHasVolatileResources = false;

#if VULKAN_VALIDATE_DESCRIPTORS_WRITTEN
	WrittenMask = BaseWrittenMask;
#endif
}
void FVulkanDescriptorSetWriter::SetWritten(uint32 DescriptorIndex)
{
#if VULKAN_VALIDATE_DESCRIPTORS_WRITTEN
	uint32 Index = DescriptorIndex / 32;
	uint32 Mask = DescriptorIndex % 32;
	WrittenMask[Index] |= (1<<Mask);
#endif
}
void FVulkanDescriptorSetWriter::SetWrittenBase(uint32 DescriptorIndex)
{
#if VULKAN_VALIDATE_DESCRIPTORS_WRITTEN	
	uint32 Index = DescriptorIndex / 32;
	uint32 Mask = DescriptorIndex % 32;
	BaseWrittenMask[Index] |= (1<<Mask);
#endif
}

void FVulkanDescriptorSetWriter::InitWrittenMasks(uint32 NumDescriptorWrites)
{
#if VULKAN_VALIDATE_DESCRIPTORS_WRITTEN	
	uint32 Size = (NumDescriptorWrites + 31) / 32;
	WrittenMask.Empty(Size);
	WrittenMask.SetNumZeroed(Size);
	BaseWrittenMask.Empty(Size);
	BaseWrittenMask.SetNumZeroed(Size);
#endif
}

void FVulkanCommonPipelineDescriptorState::SetSRV(FVulkanCmdBuffer* CmdBuffer, bool bCompute, uint8 DescriptorSet, uint32 BindingIndex, FVulkanShaderResourceView* SRV)
{
	check(!bUseBindless);

	ERHIAccess Access = bCompute
		? ERHIAccess::SRVCompute
		: ERHIAccess::SRVGraphics;

	switch (SRV->GetViewType())
	{
	case FVulkanView::EType::Null:
		checkf(false, TEXT("Attempt to bind a null SRV."));
		break;
		
	case FVulkanView::EType::TypedBuffer:
		MarkDirty(DSWriter[DescriptorSet].WriteUniformTexelBuffer(BindingIndex, SRV->GetTypedBufferView()));
		break;

	case FVulkanView::EType::Texture:
		{
			const FVulkanTexture* VulkanTexture = ResourceCast(SRV->GetTexture());
			const VkImageLayout Layout = FVulkanLayoutManager::GetDefaultLayout(CmdBuffer, *VulkanTexture, Access);
			MarkDirty(DSWriter[DescriptorSet].WriteImage(BindingIndex, SRV->GetTextureView(), Layout));
		}
		break;

	case FVulkanView::EType::StructuredBuffer:
		check((ResourceCast(SRV->GetBuffer())->GetBufferUsageFlags() & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) == VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
		MarkDirty(DSWriter[DescriptorSet].WriteStorageBuffer(BindingIndex, SRV->GetStructuredBufferView()));
		break;

	case FVulkanView::EType::AccelerationStructure:
		MarkDirty(DSWriter[DescriptorSet].WriteAccelerationStructure(BindingIndex, SRV->GetAccelerationStructureView().Handle));
		break;
	}
}

void FVulkanCommonPipelineDescriptorState::SetUAV(FVulkanCmdBuffer* CmdBuffer, bool bCompute, uint8 DescriptorSet, uint32 BindingIndex, FVulkanUnorderedAccessView* UAV)
{
	check(!bUseBindless);

	ERHIAccess Access = bCompute
		? ERHIAccess::UAVCompute
		: ERHIAccess::UAVGraphics;

	switch (UAV->GetViewType())
	{
	case FVulkanView::EType::Null:
		checkf(false, TEXT("Attempt to bind a null UAV."));
		break;

	case FVulkanView::EType::TypedBuffer:
		MarkDirty(DSWriter[DescriptorSet].WriteStorageTexelBuffer(BindingIndex, UAV->GetTypedBufferView()));
		break;

	case FVulkanView::EType::Texture:
		{
			const FVulkanTexture* VulkanTexture = ResourceCast(UAV->GetTexture());
			const VkImageLayout ExpectedLayout = FVulkanLayoutManager::GetDefaultLayout(CmdBuffer, *VulkanTexture, Access);
			MarkDirty(DSWriter[DescriptorSet].WriteStorageImage(BindingIndex, UAV->GetTextureView(), ExpectedLayout));
		}
		break;


	case FVulkanView::EType::StructuredBuffer:
		check((ResourceCast(UAV->GetBuffer())->GetBufferUsageFlags() & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) == VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
		MarkDirty(DSWriter[DescriptorSet].WriteStorageBuffer(BindingIndex, UAV->GetStructuredBufferView()));
		break;

	case FVulkanView::EType::AccelerationStructure:
		MarkDirty(DSWriter[DescriptorSet].WriteAccelerationStructure(BindingIndex, UAV->GetAccelerationStructureView().Handle));
		break;
	}
}
