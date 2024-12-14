// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VulkanUniformBuffer.cpp: Vulkan Constant buffer implementation.
=============================================================================*/

#include "VulkanRHIPrivate.h"
#include "VulkanContext.h"
#include "VulkanLLM.h"
#include "ShaderParameterStruct.h"
#include "RHIUniformBufferDataShared.h"
#include "VulkanDescriptorSets.h"

static int32 GVulkanAllowUniformUpload = 1;
static FAutoConsoleVariableRef CVarVulkanAllowUniformUpload(
	TEXT("r.Vulkan.AllowUniformUpload"),
	GVulkanAllowUniformUpload,
	TEXT("Allow Uniform Buffer uploads outside of renderpasses\n")
	TEXT(" 0: Disabled, buffers are always reallocated\n")
	TEXT(" 1: Enabled, buffers are uploaded outside renderpasses"),
	ECVF_Default
);

enum
{
	PackedUniformsRingBufferSize = 16 * 1024 * 1024
};

/*-----------------------------------------------------------------------------
	Uniform buffer RHI object
-----------------------------------------------------------------------------*/

static void UpdateUniformBufferConstants(FVulkanDevice* Device, void* DestinationData, const void* SourceData, const FRHIUniformBufferLayout* Layout)
{
	UE::RHICore::UpdateUniformBufferConstants(DestinationData, SourceData, *Layout, Device->SupportsBindless());
}

static bool UseTemporaryBuffer(EUniformBufferUsage Usage)
{
	// Add a cvar to control this behavior?
	return (Usage == UniformBuffer_SingleDraw || Usage == UniformBuffer_SingleFrame);
}

static void UpdateUniformBufferHelper(FVulkanCommandListContext& Context, FVulkanUniformBuffer* VulkanUniformBuffer, const void* Data, bool bUpdateConstants = true)
{
	FVulkanCmdBuffer* CmdBuffer = Context.GetCommandBufferManager()->GetActiveCmdBufferDirect();

	FVulkanDevice* Device = Context.GetDevice();
	const int32 DataSize = VulkanUniformBuffer->GetLayout().ConstantBufferSize;
	const int32 DataAlignment = FMath::Max<uint32>(Device->GetLimits().minUniformBufferOffsetAlignment, 16u);

	VulkanRHI::FVulkanAllocation TempAllocation;
	void* DestinationData = Context.GetTempBlockAllocator().Alloc(DataSize, DataAlignment, CmdBuffer, TempAllocation);

	if (bUpdateConstants)
	{
		// Update constants as the data is copied
		UpdateUniformBufferConstants(Device, DestinationData, Data, VulkanUniformBuffer->GetLayoutPtr());
	}
	else
	{
		// Don't touch constant, copy the data as-is
		FMemory::Memcpy(DestinationData, Data, DataSize);
	}


	if (UseTemporaryBuffer(VulkanUniformBuffer->Usage))
	{
		VulkanUniformBuffer->Allocation.Init(
			VulkanRHI::EVulkanAllocationEmpty,
			VulkanRHI::EVulkanAllocationMetaUnknown,
			TempAllocation.VulkanHandle,
			DataSize,
			TempAllocation.Offset,
			TempAllocation.AllocatorIndex,
			TempAllocation.AllocationIndex,
			TempAllocation.HandleId);
	}
	else
	{
		check(CmdBuffer->IsOutsideRenderPass());

		VkBufferCopy Region;
		Region.size = DataSize;
		Region.srcOffset = TempAllocation.Offset;
		Region.dstOffset = VulkanUniformBuffer->GetOffset();
		VkBuffer UBBuffer = VulkanUniformBuffer->Allocation.GetBufferHandle();

		VulkanRHI::vkCmdCopyBuffer(CmdBuffer->GetHandle(), TempAllocation.GetBufferHandle(), UBBuffer, 1, &Region);
	}
};

void FVulkanUniformBuffer::SetupUniformBufferView()
{
	if (UniformViewSRV && GetBufferHandle() == VK_NULL_HANDLE)
	{
		const FRHIViewDesc::FBufferSRV& SRVInfo = UniformViewSRV->GetDesc().Buffer.SRV;
		FVulkanResourceMultiBuffer* Buffer = ResourceCast(UniformViewSRV->GetBuffer());
		Allocation.Reference(Buffer->GetCurrentAllocation());
		check(Allocation.Size >= PLATFORM_MAX_UNIFORM_BUFFER_RANGE);
		//Adjust Allocation.Size ???
		Allocation.Offset += SRVInfo.OffsetInBytes;
	}
}

FVulkanUniformBuffer::FVulkanUniformBuffer(FVulkanDevice& InDevice, const FRHIUniformBufferLayout* InLayout, const void* Contents, EUniformBufferUsage InUsage, EUniformBufferValidation Validation)
	: FRHIUniformBuffer(InLayout)
	, Device(&InDevice)
	, Usage(InUsage)
	, UniformViewSRV(nullptr)
{
#if VULKAN_ENABLE_AGGRESSIVE_STATS
	SCOPE_CYCLE_COUNTER(STAT_VulkanUniformBufferCreateTime);
#endif

	// Verify the correctness of our thought pattern how the resources are delivered
	//	- If we have at least one resource, we also expect ResourceOffset to have an offset
	//	- Meaning, there is always a uniform buffer with a size specified larged than 0 bytes
	check(InLayout->Resources.Num() > 0 || InLayout->ConstantBufferSize > 0);
	const uint32 NumResources = InLayout->Resources.Num();

	// Setup resource table
	if (NumResources > 0)
	{
		// Transfer the resource table to an internal resource-array
		ResourceTable.Empty(NumResources);
		ResourceTable.AddZeroed(NumResources);

		if (Contents)
		{
			for (uint32 Index = 0; Index < NumResources; ++Index)
			{
				ResourceTable[Index] = GetShaderParameterResourceRHI(Contents, InLayout->Resources[Index].MemberOffset, InLayout->Resources[Index].MemberType);
			}
		}
	}

	if (EnumHasAnyFlags(InLayout->Flags, ERHIUniformBufferFlags::UniformView))
	{
		// For uniform view we expect an buffer SRV as a first resource
		check(InLayout->Resources.Num() > 0);
		EUniformBufferBaseType ResourceBaseType = InLayout->Resources[0].MemberType;
		if (ResourceBaseType == UBMT_SRV || ResourceBaseType == UBMT_RDG_BUFFER_SRV)
		{
			UniformViewSRV = (FRHIShaderResourceView*)GetShaderParameterResourceRHI(Contents, InLayout->Resources[0].MemberOffset, ResourceBaseType);
		}
		check(UniformViewSRV)
		return;
	}

	if (InLayout->ConstantBufferSize > 0)
	{
		const bool bInRenderingThread = IsInRenderingThread();
		const bool bInRHIThread = IsInRHIThread();

PRAGMA_DISABLE_DEPRECATION_WARNINGS
		if (UseTemporaryBuffer(InUsage) && (bInRenderingThread || bInRHIThread)
			// :todo-jn:  Temporary check until we have a command list arg passed in to avoid a race where the RenderThread
			// would pick up other tasks (because of task retraction) and execute them as if on the RenderThread.
			&& !UE::Tasks::Private::IsThreadRetractingTask())
PRAGMA_ENABLE_DEPRECATION_WARNINGS
		{
			if (Contents)
			{
				FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
				FVulkanUniformBuffer* UniformBuffer = this;
				int32 DataSize = InLayout->ConstantBufferSize;

				// make sure we allocate from RingBuffer on RHIT
				const bool bCanAllocOnThisThread = RHICmdList.Bypass() || (!IsRunningRHIInSeparateThread() && bInRenderingThread) || bInRHIThread;
				if (bCanAllocOnThisThread)
				{
					FVulkanCommandListContextImmediate& Context = Device->GetImmediateContext();
					UpdateUniformBufferHelper(Context, UniformBuffer, Contents);
				}
				else
				{
					void* CmdListConstantBufferData = RHICmdList.Alloc(DataSize, 16);
					UpdateUniformBufferConstants(Device, CmdListConstantBufferData, Contents, InLayout);

					RHICmdList.EnqueueLambda([UniformBuffer, DataSize, CmdListConstantBufferData](FRHICommandList& CmdList)
					{
						FVulkanCommandListContext& Context = FVulkanCommandListContext::GetVulkanContext(CmdList.GetContext());
						UpdateUniformBufferHelper(Context, UniformBuffer, CmdListConstantBufferData, false);
					});

					RHICmdList.RHIThreadFence(true);
				}
			}
		}
		else
		{
			VulkanRHI::FMemoryManager& ResourceMgr = Device->GetMemoryManager();
			// Set it directly as there is no previous one
			ResourceMgr.AllocUniformBuffer(Allocation, InLayout->ConstantBufferSize);
			if (Contents)
			{
				UpdateUniformBufferConstants(Device, Allocation.GetMappedPointer(Device), Contents, InLayout);
				Allocation.FlushMappedMemory(Device);
			}
		}

	}
}

FVulkanUniformBuffer::~FVulkanUniformBuffer()
{
	if (BindlessHandle.IsValid())
	{
		Device->GetDeferredDeletionQueue().EnqueueBindlessHandle(BindlessHandle);
	}

	Device->GetMemoryManager().FreeUniformBuffer(Allocation);
}

void FVulkanUniformBuffer::UpdateResourceTable(const FRHIUniformBufferLayout& InLayout, const void* Contents, int32 NumResources)
{
	check(ResourceTable.Num() == NumResources);

	for (int32 Index = 0; Index < NumResources; ++Index)
	{
		const auto Parameter = InLayout.Resources[Index];
		ResourceTable[Index] = GetShaderParameterResourceRHI(Contents, Parameter.MemberOffset, Parameter.MemberType);
	}
}

void FVulkanUniformBuffer::UpdateResourceTable(FRHIResource** Resources, int32 ResourceNum)
{
	check(ResourceTable.Num() == ResourceNum);

	for (int32 ResourceIndex = 0; ResourceIndex < ResourceNum; ++ResourceIndex)
	{
		ResourceTable[ResourceIndex] = Resources[ResourceIndex];
	}
}

FRHIDescriptorHandle FVulkanUniformBuffer::GetBindlessHandle()
{
	// :todo-jn: temporary code to refresh as needed, only used by raytracing
	const VkDeviceAddress CurrentAddress = GetDeviceAddress();
	if (!BindlessHandle.IsValid() || (CachedDeviceAddress == 0) || (CurrentAddress != CachedDeviceAddress))
	{
		if (BindlessHandle.IsValid())
		{
			Device->GetDeferredDeletionQueue().EnqueueBindlessHandle(BindlessHandle);
		}

		BindlessHandle = Device->GetBindlessDescriptorManager()->ReserveDescriptor(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
		Device->GetBindlessDescriptorManager()->UpdateBuffer(BindlessHandle, CurrentAddress, GetSize(), true);

		CachedDeviceAddress = CurrentAddress;
	}

	return BindlessHandle;
}

VkDeviceAddress FVulkanUniformBuffer::GetDeviceAddress() const
{
	// :todo-jn: there will be more and more churn on this, cache the value
	VkBufferDeviceAddressInfo BufferInfo;
	ZeroVulkanStruct(BufferInfo, VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO);
	BufferInfo.buffer = GetBufferHandle();
	VkDeviceAddress BufferAddress = VulkanRHI::vkGetBufferDeviceAddressKHR(Device->GetInstanceHandle(), &BufferInfo);
	BufferAddress += GetOffset();
	return BufferAddress;
}

FUniformBufferRHIRef FVulkanDynamicRHI::RHICreateUniformBuffer(const void* Contents, const FRHIUniformBufferLayout* Layout, EUniformBufferUsage Usage, EUniformBufferValidation Validation)
{
	LLM_SCOPE_VULKAN(ELLMTagVulkan::VulkanUniformBuffers);

	return new FVulkanUniformBuffer(*Device, Layout, Contents, Usage, Validation);
}

inline void FVulkanDynamicRHI::UpdateUniformBuffer(FRHICommandListBase& RHICmdList, FVulkanUniformBuffer* UniformBuffer, const void* Contents)
{
	SCOPE_CYCLE_COUNTER(STAT_VulkanUpdateUniformBuffers);

	const FRHIUniformBufferLayout& Layout = UniformBuffer->GetLayout();

	const int32 ConstantBufferSize = Layout.ConstantBufferSize;
	const int32 NumResources = Layout.Resources.Num();

	VulkanRHI::FVulkanAllocation NewUBAlloc;
	bool bUseUpload = GVulkanAllowUniformUpload && !RHICmdList.IsInsideRenderPass(); //inside renderpasses, a rename is enforced.
	const bool bUseTempBuffer = UseTemporaryBuffer(UniformBuffer->Usage);

	if (!bUseUpload && !bUseTempBuffer)
	{
		if (ConstantBufferSize > 0)
		{
			SCOPE_CYCLE_COUNTER(STAT_VulkanUpdateUniformBuffersRename);
			Device->GetMemoryManager().AllocUniformBuffer(NewUBAlloc, ConstantBufferSize);
			if (Contents)
			{
				UpdateUniformBufferConstants(Device, NewUBAlloc.GetMappedPointer(Device), Contents, &Layout);
				NewUBAlloc.FlushMappedMemory(Device);
			}
		}
	}

	bool bRHIBypass = RHICmdList.Bypass();
	if (bRHIBypass)
	{
		if (ConstantBufferSize > 0)
		{
			if (bUseUpload || bUseTempBuffer)
			{			
				FVulkanCommandListContext& Context = Device->GetImmediateContext();
				UpdateUniformBufferHelper(Context, UniformBuffer, Contents);
			}
			else
			{
				UniformBuffer->UpdateAllocation(NewUBAlloc);
				Device->GetMemoryManager().FreeUniformBuffer(NewUBAlloc);
			}
		}

		UniformBuffer->UpdateResourceTable(Layout, Contents, NumResources);
	}
	else
	{
		FRHIResource** CmdListResources = nullptr;
		if (NumResources > 0)
		{
			CmdListResources = (FRHIResource**)RHICmdList.Alloc(sizeof(FRHIResource*) * NumResources, alignof(FRHIResource*));

			for (int32 Index = 0; Index < NumResources; ++Index)
			{
				CmdListResources[Index] = GetShaderParameterResourceRHI(Contents, Layout.Resources[Index].MemberOffset, Layout.Resources[Index].MemberType);
			}
		}

		if (bUseUpload || bUseTempBuffer)
		{
			void* CmdListConstantBufferData = RHICmdList.Alloc(ConstantBufferSize, 16);
			FMemory::Memcpy(CmdListConstantBufferData, Contents, ConstantBufferSize);

			FVulkanCommandListContextImmediate* Context = &Device->GetImmediateContext();

			RHICmdList.EnqueueLambda([UniformBuffer, CmdListResources, NumResources, ConstantBufferSize, CmdListConstantBufferData](FRHICommandListBase& CmdList)
			{
				FVulkanCommandListContext& Context = (FVulkanCommandListContext&)CmdList.GetContext().GetLowestLevelContext();
				UpdateUniformBufferHelper(Context, UniformBuffer, CmdListConstantBufferData);
				UniformBuffer->UpdateResourceTable(CmdListResources, NumResources);
			});
		}
		else
		{
			NewUBAlloc.Disown(); //this releases ownership while its put into the lambda
			RHICmdList.EnqueueLambda([UniformBuffer, NewUBAlloc, CmdListResources, NumResources](FRHICommandListBase& CmdList)
			{
				VulkanRHI::FVulkanAllocation Alloc;
				Alloc.Reference(NewUBAlloc);
				Alloc.Own(); //this takes ownership of the allocation
				UniformBuffer->UpdateAllocation(Alloc);
				UniformBuffer->Device->GetMemoryManager().FreeUniformBuffer(Alloc);
				UniformBuffer->UpdateResourceTable(CmdListResources, NumResources);
			});
		}
		
		RHICmdList.RHIThreadFence(true);
	}
}


void FVulkanDynamicRHI::RHIUpdateUniformBuffer(FRHICommandListBase& RHICmdList, FRHIUniformBuffer* UniformBufferRHI, const void* Contents)
{
	FVulkanUniformBuffer* UniformBuffer = ResourceCast(UniformBufferRHI);
	UpdateUniformBuffer(RHICmdList, UniformBuffer, Contents);
}

