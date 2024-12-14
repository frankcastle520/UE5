// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VulkanIndexBuffer.cpp: Vulkan Index buffer RHI implementation.
=============================================================================*/

#include "VulkanRHIPrivate.h"
#include "VulkanDevice.h"
#include "VulkanContext.h"
#include "Containers/ResourceArray.h"
#include "VulkanLLM.h"
#include "VulkanRayTracing.h"
#include "VulkanTransientResourceAllocator.h"
#include "RHICoreStats.h"

struct FVulkanPendingBufferLock
{
	VulkanRHI::FStagingBuffer* StagingBuffer = nullptr;
	uint32 Offset = 0;
	uint32 Size = 0;
	EResourceLockMode LockMode = RLM_Num;
};

static TMap<FVulkanResourceMultiBuffer*, FVulkanPendingBufferLock> GPendingLocks;
static FCriticalSection GPendingLockMutex;

int32 GVulkanForceStagingBufferOnLock = 0;
static FAutoConsoleVariableRef CVarVulkanForceStagingBufferOnLock(
	TEXT("r.Vulkan.ForceStagingBufferOnLock"),
	GVulkanForceStagingBufferOnLock,
	TEXT("When nonzero, non-volatile buffer locks will always use staging buffers. Useful for debugging.\n")
	TEXT("default: 0"),
	ECVF_RenderThreadSafe
);

static FORCEINLINE FVulkanPendingBufferLock GetPendingBufferLock(FVulkanResourceMultiBuffer* Buffer)
{
	FVulkanPendingBufferLock PendingLock;

	// Found only if it was created for Write
	FScopeLock ScopeLock(&GPendingLockMutex);
	const bool bFound = GPendingLocks.RemoveAndCopyValue(Buffer, PendingLock);

	checkf(bFound, TEXT("Mismatched Buffer Lock/Unlock!"));
	return PendingLock;
}

static FORCEINLINE void AddPendingBufferLock(FVulkanResourceMultiBuffer* Buffer, FVulkanPendingBufferLock& PendingLock)
{
	FScopeLock ScopeLock(&GPendingLockMutex);
	check(!GPendingLocks.Contains(Buffer));
	GPendingLocks.Add(Buffer, PendingLock);
}

static void UpdateVulkanBufferStats(const FRHIBufferDesc& BufferDesc, int64 BufferSize, bool bAllocating)
{
	UE::RHICore::UpdateGlobalBufferStats(BufferDesc, BufferSize, bAllocating);
}

static VkDeviceAddress GetBufferDeviceAddress(FVulkanDevice* Device, VkBuffer Buffer)
{
	if (Device->GetOptionalExtensions().HasBufferDeviceAddress)
	{
		VkBufferDeviceAddressInfoKHR DeviceAddressInfo;
		ZeroVulkanStruct(DeviceAddressInfo, VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO);
		DeviceAddressInfo.buffer = Buffer;
		return VulkanRHI::vkGetBufferDeviceAddressKHR(Device->GetInstanceHandle(), &DeviceAddressInfo);
	}
	return 0;
}

VkBufferUsageFlags FVulkanResourceMultiBuffer::UEToVKBufferUsageFlags(FVulkanDevice* InDevice, EBufferUsageFlags InUEUsage, bool bZeroSize)
{
	// Always include TRANSFER_SRC since hardware vendors confirmed it wouldn't have any performance cost and we need it for some debug functionalities.
	VkBufferUsageFlags OutVkUsage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

	auto TranslateFlag = [&OutVkUsage, &InUEUsage](EBufferUsageFlags SearchUEFlag, VkBufferUsageFlags AddedIfFound, VkBufferUsageFlags AddedIfNotFound = 0)
	{
		const bool HasFlag = EnumHasAnyFlags(InUEUsage, SearchUEFlag);
		OutVkUsage |= HasFlag ? AddedIfFound : AddedIfNotFound;
	};

	TranslateFlag(BUF_VertexBuffer, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
	TranslateFlag(BUF_IndexBuffer, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
	TranslateFlag(BUF_StructuredBuffer, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
	TranslateFlag(BUF_UniformBuffer, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
	TranslateFlag(BUF_AccelerationStructure, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR);

	if (!bZeroSize)
	{
		TranslateFlag(BUF_UnorderedAccess, VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT);
		TranslateFlag(BUF_DrawIndirect, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
		TranslateFlag(BUF_KeepCPUAccessible, (VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT));
		TranslateFlag(BUF_ShaderResource, VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT);

		TranslateFlag(BUF_Volatile, 0, VK_BUFFER_USAGE_TRANSFER_DST_BIT);

		if (InDevice->GetOptionalExtensions().HasRaytracingExtensions())
		{
			OutVkUsage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

			TranslateFlag(BUF_AccelerationStructure, 0, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR);
		}

		// For descriptors buffers
		if (InDevice->GetOptionalExtensions().HasBufferDeviceAddress)
		{
			OutVkUsage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
		}
	}

	return OutVkUsage;
}

FVulkanResourceMultiBuffer::FVulkanResourceMultiBuffer(FVulkanDevice* InDevice, FRHIBufferDesc const& InBufferDesc, FRHIResourceCreateInfo& CreateInfo, FRHICommandListBase* InRHICmdList, const FRHITransientHeapAllocation* InTransientHeapAllocation)
	: FRHIBuffer(InBufferDesc)
	, VulkanRHI::FDeviceChild(InDevice)
{
	VULKAN_TRACK_OBJECT_CREATE(FVulkanResourceMultiBuffer, this);

	const bool bZeroSize = (InBufferDesc.Size == 0);
	BufferUsageFlags = UEToVKBufferUsageFlags(InDevice, InBufferDesc.Usage, bZeroSize);
	
	#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (CreateInfo.DebugName)
	{
		SetName(CreateInfo.DebugName);
	}
	#endif

	if (!bZeroSize)
	{
		check(InDevice);

		const bool bUnifiedMem = InDevice->HasUnifiedMemory();
		const uint32 BufferAlignment = VulkanRHI::FMemoryManager::CalculateBufferAlignment(*InDevice, InBufferDesc.Usage, bZeroSize);

		if (InTransientHeapAllocation != nullptr)
		{
			CurrentBufferAlloc.Alloc = FVulkanTransientHeap::GetVulkanAllocation(*InTransientHeapAllocation);
			CurrentBufferAlloc.HostPtr = bUnifiedMem ? CurrentBufferAlloc.Alloc.GetMappedPointer(Device) : nullptr;
			CurrentBufferAlloc.DeviceAddress = GetBufferDeviceAddress(InDevice, CurrentBufferAlloc.Alloc.GetBufferHandle()) + CurrentBufferAlloc.Alloc.Offset;
			check(CurrentBufferAlloc.Alloc.Offset % BufferAlignment == 0);
			check(CurrentBufferAlloc.Alloc.Size >= InBufferDesc.Size);
		}
		else
		{
			AllocateMemory(CurrentBufferAlloc);
		}

		if (CreateInfo.ResourceArray)
		{
			const uint32 CopyDataSize = FMath::Min(InBufferDesc.Size, CreateInfo.ResourceArray->GetResourceDataSize());

			// We know this buffer is not in use by GPU atm. If we do have a direct access initialize it without extra copies
			if (CurrentBufferAlloc.HostPtr)
			{
				FMemory::Memcpy(CurrentBufferAlloc.HostPtr, CreateInfo.ResourceArray->GetResourceData(), CopyDataSize);
				++LockCounter;
			}
			else
			{
				check(InRHICmdList);
				void* Data = Lock(*InRHICmdList, RLM_WriteOnly, CopyDataSize, 0);
				FMemory::Memcpy(Data, CreateInfo.ResourceArray->GetResourceData(), CopyDataSize);
				Unlock(*InRHICmdList);
			}

			CreateInfo.ResourceArray->Discard();
		}
	}
}

FVulkanResourceMultiBuffer::~FVulkanResourceMultiBuffer()
{
	VULKAN_TRACK_OBJECT_DELETE(FVulkanResourceMultiBuffer, this);
	ReleaseOwnership();
}


void FVulkanResourceMultiBuffer::AllocateMemory(FBufferAlloc& OutAlloc)
{
	VkMemoryPropertyFlags BufferMemFlags = 0;
	const bool bUnifiedMem = Device->HasUnifiedMemory();
	const bool bDynamic = EnumHasAnyFlags(GetUsage(), BUF_Dynamic) || EnumHasAnyFlags(GetUsage(), BUF_Volatile);
	if (bUnifiedMem)
	{
		BufferMemFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	}
	else if (bDynamic)
	{
		BufferMemFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	}
	else
	{
		BufferMemFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	}
	const uint32 BufferSize = GetSize();
	const uint32 BufferAlignment = VulkanRHI::FMemoryManager::CalculateBufferAlignment(*Device, GetUsage(), (BufferSize == 0));

	FBufferAlloc& NewBufferAlloc = OutAlloc;
	if (!Device->GetMemoryManager().AllocateBufferPooled(NewBufferAlloc.Alloc, nullptr, BufferSize, BufferAlignment, BufferUsageFlags, BufferMemFlags, VulkanRHI::EVulkanAllocationMetaMultiBuffer, __FILE__, __LINE__))
	{
		Device->GetMemoryManager().HandleOOM();
	}
	NewBufferAlloc.HostPtr = (bUnifiedMem || bDynamic) ? NewBufferAlloc.Alloc.GetMappedPointer(Device) : nullptr;
	NewBufferAlloc.DeviceAddress = GetBufferDeviceAddress(Device, NewBufferAlloc.Alloc.GetBufferHandle()) + NewBufferAlloc.Alloc.Offset;

	UpdateVulkanBufferStats(GetDesc(), BufferSize, true);
}

void* FVulkanResourceMultiBuffer::Lock(FRHICommandListBase& RHICmdList, EResourceLockMode LockMode, uint32 LockSize, uint32 Offset)
{
	void* Data = nullptr;
	uint32 DataOffset = 0;

	check(LockStatus == ELockStatus::Unlocked);

	LockStatus = ELockStatus::Locked;
	const bool bIsFirstLock = (0 == LockCounter++);

	// Dynamic:    Allocate a new Host_Visible buffer, swap this new buffer in on RHI thread and update views.  
	//             GPU reads directly from host memory, but no copy is required so it can be used in render passes.
	// Static:     A single Device_Local buffer is allocated at creation.  For Lock/Unlock, use a staging buffer for the upload:
	//             host writes to staging buffer on lock, a copy on GPU is issued on unlock to update the device_local memory.

	const bool bUnifiedMem = Device->HasUnifiedMemory();
	const bool bDynamic = EnumHasAnyFlags(GetUsage(), BUF_Dynamic) || EnumHasAnyFlags(GetUsage(), BUF_Volatile);
	const bool bStatic = EnumHasAnyFlags(GetUsage(), BUF_Static) || !bDynamic;
	const bool bUAV = EnumHasAnyFlags(GetUsage(), BUF_UnorderedAccess);
	const bool bSR = EnumHasAnyFlags(GetUsage(), BUF_ShaderResource);

	check(bStatic || bDynamic || bUAV || bSR);

	if (LockMode == RLM_ReadOnly)
	{
		check(IsInRenderingThread());

		if (bUnifiedMem)
		{
			Data = CurrentBufferAlloc.HostPtr;
			DataOffset = Offset;
			LockStatus = ELockStatus::PersistentMapping;
		}
		else 
		{
			Device->PrepareForCPURead();
		
			FVulkanCommandListContextImmediate& Context = Device->GetImmediateContext();
			FVulkanCommandBufferManager* CommandBufferManager = Context.GetCommandBufferManager();
			FVulkanCmdBuffer* CmdBuffer = CommandBufferManager->GetUploadCmdBuffer();
				
			// Make sure any previous tasks have finished on the source buffer.
			VkMemoryBarrier BarrierBefore = { VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT };
			VulkanRHI::vkCmdPipelineBarrier(CmdBuffer->GetHandle(), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &BarrierBefore, 0, nullptr, 0, nullptr);

			// Create a staging buffer we can use to copy data from device to cpu.
			VulkanRHI::FStagingBuffer* StagingBuffer = Device->GetStagingManager().AcquireBuffer(LockSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT);

			// Fill the staging buffer with the data on the device.
			VkBufferCopy Regions;
			Regions.size = LockSize;
			Regions.srcOffset = Offset + CurrentBufferAlloc.Alloc.Offset;
			Regions.dstOffset = 0;
				
			VulkanRHI::vkCmdCopyBuffer(CmdBuffer->GetHandle(), CurrentBufferAlloc.Alloc.GetBufferHandle(), StagingBuffer->GetHandle(), 1, &Regions);

			// Setup barrier.
			VkMemoryBarrier BarrierAfter = { VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_HOST_READ_BIT };
			VulkanRHI::vkCmdPipelineBarrier(CmdBuffer->GetHandle(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &BarrierAfter, 0, nullptr, 0, nullptr);
				
			// Force upload.
			CommandBufferManager->SubmitUploadCmdBuffer();
			Device->WaitUntilIdle();

			// Flush.
			StagingBuffer->FlushMappedMemory();

			// Get mapped pointer. 
			Data = StagingBuffer->GetMappedPointer();

			// Release temp staging buffer during unlock.
			FVulkanPendingBufferLock PendingLock;
			PendingLock.Offset = 0;
			PendingLock.Size = LockSize;
			PendingLock.LockMode = LockMode;
			PendingLock.StagingBuffer = StagingBuffer;
			AddPendingBufferLock(this, PendingLock);

			CommandBufferManager->PrepareForNewActiveCommandBuffer();
		}
	}
	else
	{
		check(LockMode == RLM_WriteOnly);

		// If this is the first lock on host visible memory, then the memory is still untouched so use it directly
		if ((bUnifiedMem || bDynamic) && bIsFirstLock)
		{
			check(CurrentBufferAlloc.HostPtr);
			Data = CurrentBufferAlloc.HostPtr;
			DataOffset = Offset;
			LockStatus = ELockStatus::PersistentMapping;
		}
		else if (bStatic || GVulkanForceStagingBufferOnLock)
		{
			FVulkanPendingBufferLock PendingLock;
			PendingLock.Offset = Offset;
			PendingLock.Size = LockSize;
			PendingLock.LockMode = LockMode;

			VulkanRHI::FStagingBuffer* StagingBuffer = Device->GetStagingManager().AcquireBuffer(LockSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
			PendingLock.StagingBuffer = StagingBuffer;
			Data = StagingBuffer->GetMappedPointer();

			AddPendingBufferLock(this, PendingLock);
		}
		else
		{
			FBufferAlloc NewAlloc;
			AllocateMemory(NewAlloc);
			NewAlloc.Alloc.Disown();

			RHICmdList.EnqueueLambda(TEXT("FVulkanBuffer::Lock"), [Buffer = this, NewAlloc](FRHICommandListBase& CmdList)
			{
				Buffer->CurrentBufferAlloc.Alloc.Free(*Buffer->GetParent());
				Buffer->CurrentBufferAlloc = NewAlloc;
				Buffer->CurrentBufferAlloc.Alloc.Own();
				Buffer->UpdateLinkedViews();
			});

			Data = NewAlloc.HostPtr;
			DataOffset = Offset;
			LockStatus = ELockStatus::PersistentMapping;
		}
	}

	check(Data);
	return (uint8*)Data + DataOffset;
}


void FVulkanResourceMultiBuffer::Unlock(FRHICommandListBase& RHICmdList)
{
	const bool bUnifiedMem = Device->HasUnifiedMemory();
	const bool bDynamic = EnumHasAnyFlags(GetUsage(), BUF_Dynamic) || EnumHasAnyFlags(GetUsage(), BUF_Volatile);
	const bool bStatic = EnumHasAnyFlags(GetUsage(), BUF_Static) || !bDynamic;
	const bool bSR = EnumHasAnyFlags(GetUsage(), BUF_ShaderResource);

	check(LockStatus != ELockStatus::Unlocked);

	if (LockStatus == ELockStatus::PersistentMapping)
	{
		// Do nothing
	}
	else
	{
		check(bStatic || bDynamic || bSR);

		FVulkanPendingBufferLock PendingLock = GetPendingBufferLock(this);

		RHICmdList.EnqueueLambda(TEXT("FVulkanBuffer::Unlock"), [Buffer=this, PendingLock](FRHICommandListBase& CmdList)
		{
			VulkanRHI::FStagingBuffer* StagingBuffer = PendingLock.StagingBuffer;
			check(StagingBuffer);
			StagingBuffer->FlushMappedMemory();

			if (PendingLock.LockMode == RLM_ReadOnly)
			{
				// Just remove the staging buffer here.
				Buffer->Device->GetStagingManager().ReleaseBuffer(nullptr, StagingBuffer);
			}
			else if (PendingLock.LockMode == RLM_WriteOnly)
			{
				FVulkanCommandListContext& Context = FVulkanCommandListContext::GetVulkanContext(CmdList.GetContext());

				// We need to do this on the active command buffer instead of using an upload command buffer. The high level code sometimes reuses the same
				// buffer in sequences of upload / dispatch, upload / dispatch, so we need to order the copy commands correctly with respect to the dispatches.
				FVulkanCmdBuffer* Cmd = Context.GetCommandBufferManager()->GetActiveCmdBuffer();
				check(Cmd && Cmd->IsOutsideRenderPass());
				VkCommandBuffer CmdBuffer = Cmd->GetHandle();

				VulkanRHI::DebugHeavyWeightBarrier(CmdBuffer, 16);

				VkBufferCopy Region;
				FMemory::Memzero(Region);
				Region.size = PendingLock.Size;
				//Region.srcOffset = 0;
				Region.dstOffset = PendingLock.Offset + Buffer->CurrentBufferAlloc.Alloc.Offset;
				VulkanRHI::vkCmdCopyBuffer(CmdBuffer, StagingBuffer->GetHandle(), Buffer->CurrentBufferAlloc.Alloc.GetBufferHandle(), 1, &Region);

				// High level code expects the data in Buffer to be ready to read
				VkMemoryBarrier BarrierAfter = { VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT };
				VulkanRHI::vkCmdPipelineBarrier(CmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &BarrierAfter, 0, nullptr, 0, nullptr);

				Buffer->GetParent()->GetStagingManager().ReleaseBuffer(Cmd, StagingBuffer);

				Buffer->UpdateLinkedViews(); // :todo-jn:  not needed?  Same buffer when we use staging?
			}
		});
	}

	LockStatus = ELockStatus::Unlocked;
}

void FVulkanResourceMultiBuffer::TakeOwnership(FVulkanResourceMultiBuffer& Other)
{
	check(Other.LockStatus == ELockStatus::Unlocked);
	check(GetParent() == Other.GetParent());

	// Clean up any resource this buffer already owns
	ReleaseOwnership();

	// Transfer ownership of Other's resources to this instance
	FRHIBuffer::TakeOwnership(Other);

	BufferUsageFlags   = Other.BufferUsageFlags;
	CurrentBufferAlloc = Other.CurrentBufferAlloc;

	Other.BufferUsageFlags   = {};
	Other.CurrentBufferAlloc = {};
}

void FVulkanResourceMultiBuffer::ReleaseOwnership()
{
	check(LockStatus == ELockStatus::Unlocked);

	if (CurrentBufferAlloc.Alloc.HasAllocation())
	{
		UpdateVulkanBufferStats(GetDesc(), CurrentBufferAlloc.Alloc.Size, false);
		Device->GetMemoryManager().FreeVulkanAllocation(CurrentBufferAlloc.Alloc);
	}

	FRHIBuffer::ReleaseOwnership();
}

FBufferRHIRef FVulkanDynamicRHI::RHICreateBuffer(FRHICommandListBase& RHICmdList, FRHIBufferDesc const& Desc, ERHIAccess ResourceState, FRHIResourceCreateInfo& CreateInfo)
{
#if VULKAN_USE_LLM
	LLM_SCOPE_VULKAN(ELLMTagVulkan::VulkanBuffers);
#else
	LLM_SCOPE(EnumHasAnyFlags(Desc.Usage, EBufferUsageFlags::VertexBuffer | EBufferUsageFlags::IndexBuffer) ? ELLMTag::Meshes : ELLMTag::RHIMisc);
#endif
	return new FVulkanResourceMultiBuffer(Device, Desc, CreateInfo, &RHICmdList);
}

void* FVulkanDynamicRHI::LockBuffer_BottomOfPipe(FRHICommandListBase& RHICmdList, FRHIBuffer* BufferRHI, uint32 Offset, uint32 Size, EResourceLockMode LockMode)
{
	LLM_SCOPE_VULKAN(ELLMTagVulkan::VulkanBuffers);
	FVulkanResourceMultiBuffer* Buffer = ResourceCast(BufferRHI);
	return Buffer->Lock(RHICmdList, LockMode, Size, Offset);
}

void FVulkanDynamicRHI::UnlockBuffer_BottomOfPipe(FRHICommandListBase& RHICmdList, FRHIBuffer* BufferRHI)
{
	LLM_SCOPE_VULKAN(ELLMTagVulkan::VulkanBuffers);
	FVulkanResourceMultiBuffer* Buffer = ResourceCast(BufferRHI);
	Buffer->Unlock(RHICmdList);
}

void* FVulkanDynamicRHI::RHILockBuffer(FRHICommandListBase& RHICmdList, FRHIBuffer* BufferRHI, uint32 Offset, uint32 Size, EResourceLockMode LockMode)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_LockBuffer_RenderThread);
	LLM_SCOPE_VULKAN(ELLMTagVulkan::VulkanBuffers);
	FVulkanResourceMultiBuffer* Buffer = ResourceCast(BufferRHI);
	return Buffer->Lock(RHICmdList, LockMode, Size, Offset);
}

void FVulkanDynamicRHI::RHIUnlockBuffer(FRHICommandListBase& RHICmdList, FRHIBuffer* BufferRHI)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_UnlockBuffer_RenderThread);
	LLM_SCOPE_VULKAN(ELLMTagVulkan::VulkanBuffers);
	FVulkanResourceMultiBuffer* Buffer = ResourceCast(BufferRHI);
	Buffer->Unlock(RHICmdList);
}
