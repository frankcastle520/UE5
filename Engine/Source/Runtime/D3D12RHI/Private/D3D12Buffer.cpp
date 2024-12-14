// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
D3D12Buffer.cpp: D3D Common code for buffers.
=============================================================================*/

#include "D3D12RHIPrivate.h"
#include "ProfilingDebugging/AssetMetadataTrace.h"
#include "RHICoreStats.h"

FName GetRHIBufferClassName(const FName& ClassName)
{
	const static FLazyName RHIBufferName(TEXT("FRHIBuffer"));
	return (ClassName == NAME_None) ? RHIBufferName : ClassName;
}

extern int32 GD3D12BindResourceLabels;

FD3D12Buffer::~FD3D12Buffer()
{
	if (EnumHasAnyFlags(GetUsage(), EBufferUsageFlags::VertexBuffer) && GetParentDevice())
	{
		FD3D12CommandContext& DefaultContext = GetParentDevice()->GetDefaultCommandContext();
		DefaultContext.StateCache.ClearVertexBuffer(&ResourceLocation);
	}

	bool bTransient = ResourceLocation.IsTransient();
	if (!bTransient)
	{
		D3D12BufferStats::UpdateBufferStats(*this, false);
	}
}

void FD3D12Buffer::UploadResourceData(FD3D12CommandContext& CommandContext, D3D12_RESOURCE_STATES InDestinationState, FD3D12ResourceLocation& DestinationResourceLocation, const FD3D12ResourceLocation& SourceResourceLocation, uint32 Size)
{
	FD3D12Resource* Destination = DestinationResourceLocation.GetResource();

	// Copy from the temporary upload heap to the default resource

	// if resource doesn't require state tracking then transition to copy dest here (could have been suballocated from shared resource) - not very optimal and should be batched
	if (!Destination->RequiresResourceStateTracking())
	{
		CommandContext.AddTransitionBarrier(Destination, Destination->GetDefaultResourceState(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
	}

	CommandContext.FlushResourceBarriers();

	CommandContext.GraphicsCommandList()->CopyBufferRegion(
		Destination->GetResource(),
		DestinationResourceLocation.GetOffsetFromBaseOfResource(),
		SourceResourceLocation.GetResource()->GetResource(),
		SourceResourceLocation.GetOffsetFromBaseOfResource(),
		Size);

	// Update the resource state after the copy has been done (will take care of updating the residency as well)
	if (InDestinationState != D3D12_RESOURCE_STATE_COPY_DEST)
	{
		CommandContext.AddTransitionBarrier(Destination, D3D12_RESOURCE_STATE_COPY_DEST, InDestinationState, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
	}

	CommandContext.UpdateResidency(SourceResourceLocation.GetResource());

	CommandContext.ConditionalSplitCommandList();

	// If the resource is untracked, the destination state must match the default state of the resource.
	check(Destination->RequiresResourceStateTracking() || (Destination->GetDefaultResourceState() == InDestinationState));

	// Buffer is now written and ready, so unlock the block (locked after creation and can be defragmented if needed)
	DestinationResourceLocation.UnlockPoolData();
}

void FD3D12Buffer::UploadResourceData(FRHICommandListBase& InRHICmdList, FRHIGPUMask GPUMask, D3D12_RESOURCE_STATES InDestinationState, const void* SourceData, int32 SourceDataSize)
{
	FD3D12Device* ParentDevice = GetParentDevice();

	const uint32 BufferSize = GetSize();
	check(BufferSize == SourceDataSize);

	const bool bOnAsyncThread = !IsInRHIThread() && !IsInRenderingThread();

	// Get an upload heap and initialize data
	FD3D12ResourceLocation SrcResourceLoc(ParentDevice);
	void* pData;
	if (bOnAsyncThread)
	{
		pData = ParentDevice->GetParentAdapter()->GetUploadHeapAllocator(ParentDevice->GetGPUIndex()).AllocUploadResource(BufferSize, 4u, SrcResourceLoc);
	}
	else
	{
		pData = ParentDevice->GetDefaultFastAllocator().Allocate(BufferSize, 4UL, &SrcResourceLoc);
	}
	check(pData);
	FMemory::Memcpy(pData, SourceData, BufferSize);

	InRHICmdList.EnqueueLambda(
		[this, GPUMask, InDestinationState, SrcResourceLoc = MoveTemp(SrcResourceLoc)](FRHICommandListBase& ExecutingCmdList)
		{
			const FRHIGPUMask EffectiveMask = GPUMask & ExecutingCmdList.GetGPUMask();
			for (uint32 GPUIndex : EffectiveMask)
			{
				FD3D12CommandContext& CommandContext = FD3D12CommandContext::Get(ExecutingCmdList, GPUIndex);
				FD3D12Buffer* DeviceBuffer = CommandContext.RetrieveObject<FD3D12Buffer>(this, GPUIndex);

				UploadResourceData(CommandContext, InDestinationState, DeviceBuffer->ResourceLocation, SrcResourceLoc, GetSize());
			}
		});
}

void FD3D12Buffer::UploadResourceData(FRHICommandListBase& RHICmdList, FResourceArrayUploadInterface* InResourceArray, D3D12_RESOURCE_STATES InDestinationState, const TCHAR* AssetName, const FName& ClassName, const FName& PackageName)
{
	UE_TRACE_METADATA_SCOPE_ASSET_FNAME(AssetName, ClassName, PackageName);

	check(InResourceArray);
	check(ResourceLocation.IsValid());

	uint32 BufferSize = GetSize();
	check(BufferSize == InResourceArray->GetResourceDataSize());

	if (EnumHasAnyFlags(GetUsage(), BUF_AnyDynamic))
	{
		// Copy directly in mapped data
		void* MappedUploadData = ResourceLocation.GetMappedBaseAddress(); 
		FMemory::Memcpy(MappedUploadData, InResourceArray->GetResourceData(), BufferSize);
	}
	else
	{
		UploadResourceData(RHICmdList, FRHIGPUMask::All(), InDestinationState, InResourceArray->GetResourceData(), BufferSize);
	}

	// Discard the resource array's contents.
	InResourceArray->Discard();
}


FD3D12SyncPointRef FD3D12Buffer::UploadResourceDataViaCopyQueue(FResourceArrayUploadInterface* InResourceArray)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UploadResourceDataViaCopyQueue);

	// assume not dynamic and not on async thread (probably fine but untested)
	check(IsInRHIThread() || IsInRenderingThread());
	check(!(GetUsage() & BUF_AnyDynamic));

	uint32 BufferSize = GetSize();

	// Get an upload heap and copy the data
	FD3D12ResourceLocation SrcResourceLoc(GetParentDevice());
	void* pData = GetParentDevice()->GetDefaultFastAllocator().Allocate(BufferSize, 4UL, &SrcResourceLoc);
	check(pData);
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CopyToUploadMemory);
		FMemory::Memcpy(pData, InResourceArray->GetResourceData(), BufferSize);
	}

	// Allocate copy queue command list and perform the copy op
	FD3D12Device* Device = SrcResourceLoc.GetParentDevice();

	FD3D12SyncPointRef SyncPoint;
	{
		FD3D12CopyScope CopyScope(Device, ED3D12SyncPointType::GPUOnly);
		SyncPoint = CopyScope.GetSyncPoint();

		// Perform actual copy op
		CopyScope.Context.CopyCommandList()->CopyBufferRegion(
			ResourceLocation.GetResource()->GetResource(),
			ResourceLocation.GetOffsetFromBaseOfResource(),
			SrcResourceLoc.GetResource()->GetResource(),
			SrcResourceLoc.GetOffsetFromBaseOfResource(), BufferSize);

		// Residency update needed since it's just been created?
		CopyScope.Context.UpdateResidency(ResourceLocation.GetResource());
	}

	// Buffer is now written and ready, so unlock the block
	ResourceLocation.UnlockPoolData();

	// Discard the resource array's contents.
	InResourceArray->Discard();

	return SyncPoint;
}


void FD3D12Adapter::AllocateBuffer(FD3D12Device* Device,
	const D3D12_RESOURCE_DESC& InDesc,
	uint32 Size,
	EBufferUsageFlags InUsage,
	ED3D12ResourceStateMode InResourceStateMode,
	D3D12_RESOURCE_STATES InCreateState,
	uint32 Alignment,
	FD3D12Buffer* Buffer,
	FD3D12ResourceLocation& ResourceLocation,
	ID3D12ResourceAllocator* ResourceAllocator,
	const TCHAR* InDebugName)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(D3D12RHI::AllocateBuffer);

	// Explicitly check that the size is nonzero before allowing CreateBuffer to opaquely fail.
	checkf(Size > 0, TEXT("Attempt to create buffer '%s' with size 0."), InDebugName ? InDebugName : TEXT("(null)"));

	if (EnumHasAnyFlags(InUsage, BUF_AnyDynamic))
	{
		check(ResourceAllocator == nullptr);
		check(InResourceStateMode != ED3D12ResourceStateMode::MultiState);
		check(InCreateState == D3D12_RESOURCE_STATE_GENERIC_READ);
		GetUploadHeapAllocator(Device->GetGPUIndex()).AllocUploadResource(Size, Alignment, ResourceLocation);
		check(ResourceLocation.GetSize() >= Size);
	}
	else
	{
		if (ResourceAllocator)
		{
			ResourceAllocator->AllocateResource(Device->GetGPUIndex(), D3D12_HEAP_TYPE_DEFAULT, InDesc, InDesc.Width, Alignment, InResourceStateMode, InCreateState, nullptr, InDebugName, ResourceLocation);
		}
		else
		{
			Device->GetDefaultBufferAllocator().AllocDefaultResource(D3D12_HEAP_TYPE_DEFAULT, InDesc, InUsage, InResourceStateMode, InCreateState, ResourceLocation, Alignment, InDebugName);
		}
		ResourceLocation.SetOwner(Buffer);
		check(ResourceLocation.GetSize() >= Size);
	}
}

FD3D12Buffer* FD3D12Adapter::CreateRHIBuffer(
	const D3D12_RESOURCE_DESC& InDesc,
	uint32 Alignment,
	FRHIBufferDesc const& BufferDesc,
	ED3D12ResourceStateMode InResourceStateMode,
	D3D12_RESOURCE_STATES InCreateState,
	bool bKeepUnlocked,
	const FRHIGPUMask& InGPUMask,
	ID3D12ResourceAllocator* ResourceAllocator,
	const TCHAR* InDebugName,
	const FName& OwnerName,
	const FName& ClassName)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(D3D12RHI::CreateRHIBuffer);
	SCOPE_CYCLE_COUNTER(STAT_D3D12CreateBufferTime);

	UE_TRACE_METADATA_SCOPE_ASSET_FNAME(FName(InDebugName), GetRHIBufferClassName(ClassName), OwnerName);

	check(InDesc.Width >= BufferDesc.Size);

	FD3D12Buffer* BufferOut = nullptr;

	if (EnumHasAnyFlags(BufferDesc.Usage, BUF_AnyDynamic))
	{
		const uint32 FirstGPUIndex = InGPUMask.GetFirstIndex();

		FD3D12Buffer* NewBuffer0 = nullptr;
		BufferOut = CreateLinkedObject<FD3D12Buffer>(InGPUMask, [&](FD3D12Device* Device, FD3D12Buffer* FirstLinkedObject)
		{
			FD3D12Buffer* NewBuffer = new FD3D12Buffer(Device, BufferDesc);
			NewBuffer->BufferAlignment = Alignment;

#if NAME_OBJECTS
			if (InDebugName)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(D3D12RHI::SetDebugName);
				NewBuffer->SetName(InDebugName);
			}
#endif // NAME_OBJECTS

			if ((Device->GetGPUIndex() == FirstGPUIndex) || EnumHasAnyFlags(BufferDesc.Usage, BUF_MultiGPUAllocate))
			{
				AllocateBuffer(Device, InDesc, BufferDesc.Size, BufferDesc.Usage, InResourceStateMode, InCreateState, Alignment, NewBuffer, NewBuffer->ResourceLocation, ResourceAllocator, InDebugName);
				NewBuffer0 = NewBuffer;
			}
			else
			{
				check(NewBuffer0);
				FD3D12ResourceLocation::ReferenceNode(Device, NewBuffer->ResourceLocation, NewBuffer0->ResourceLocation);
			}

			return NewBuffer;
		});
	}
	else
	{
		BufferOut = CreateLinkedObject<FD3D12Buffer>(InGPUMask, [&](FD3D12Device* Device, FD3D12Buffer* FirstLinkedObject)
		{
			FD3D12Buffer* NewBuffer = new FD3D12Buffer(Device, BufferDesc);
			NewBuffer->BufferAlignment = Alignment;

#if NAME_OBJECTS
			if (InDebugName)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(D3D12RHI::SetDebugName);
				NewBuffer->SetName(InDebugName);
			}
#endif // NAME_OBJECTS

			AllocateBuffer(Device, InDesc, BufferDesc.Size, BufferDesc.Usage, InResourceStateMode, InCreateState, Alignment, NewBuffer, NewBuffer->ResourceLocation, ResourceAllocator, InDebugName);
			
			// Unlock immediately if there is no initial data
			if (!bKeepUnlocked)
			{
				NewBuffer->ResourceLocation.UnlockPoolData();
			}

			return NewBuffer;
		});
	}

	// Don't track transient buffer stats here
	if (!BufferOut->ResourceLocation.IsTransient())
	{
		D3D12BufferStats::UpdateBufferStats(*BufferOut, true);
	}

	return BufferOut;
}

void FD3D12Buffer::Rename(FD3D12ContextArray const& Contexts, FD3D12ResourceLocation& NewLocation)
{
	FD3D12ResourceLocation::TransferOwnership(ResourceLocation, NewLocation);
	ResourceRenamed(Contexts);
}

void FD3D12Buffer::RenameLDAChain(FD3D12ContextArray const& Contexts, FD3D12ResourceLocation& NewLocation)
{
	// Dynamic buffers use cross-node resources (with the exception of BUF_MultiGPUAllocate)
	//ensure(GetUsage() & BUF_AnyDynamic);
	Rename(Contexts, NewLocation);

	if (GNumExplicitGPUsForRendering > 1)
	{
		ensure(GetParentDevice() == NewLocation.GetParentDevice());

		if (EnumHasAnyFlags(GetUsage(), BUF_MultiGPUAllocate) == false)
		{
			ensure(IsHeadLink());

			// Update all of the resources in the LDA chain to reference this cross-node resource
			for (auto NextBuffer = ++FLinkedObjectIterator(this); NextBuffer; ++NextBuffer)
			{
				FD3D12ResourceLocation::ReferenceNode(NextBuffer->GetParentDevice(), NextBuffer->ResourceLocation, ResourceLocation);
				NextBuffer->ResourceRenamed(Contexts);
			}
		}
	}
}

void FD3D12Buffer::TakeOwnership(FD3D12Buffer& Other)
{
	check(!Other.LockedData.bLocked);

	// Clean up any resource this buffer already owns
	ReleaseOwnership();

	// Transfer ownership of Other's resources to this instance
	FRHIBuffer::TakeOwnership(Other);
	FD3D12ResourceLocation::TransferOwnership(ResourceLocation, Other.ResourceLocation);
}

void FD3D12Buffer::ReleaseOwnership()
{
	check(!LockedData.bLocked);
	check(IsHeadLink());

	FRHIBuffer::ReleaseOwnership();

	if (!ResourceLocation.IsTransient())
	{
		D3D12BufferStats::UpdateBufferStats(*this, false);
	}

	ResourceLocation.Clear();
}

void FD3D12Buffer::GetResourceDescAndAlignment(uint64 InSize, uint32 InStride, EBufferUsageFlags InUsage, D3D12_RESOURCE_DESC& ResourceDesc, uint32& Alignment)
{
	ResourceDesc = CD3DX12_RESOURCE_DESC::Buffer(InSize);

	// Align size to 16 so RAW buffer view can be created without loosing any data at the end when dividing num elements by 4
	ResourceDesc.Width = Align(ResourceDesc.Width, 16);

	if (EnumHasAnyFlags(InUsage, BUF_UnorderedAccess))
	{
		ResourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	}

	if (!EnumHasAnyFlags(InUsage, BUF_ShaderResource | BUF_AccelerationStructure))
	{
		ResourceDesc.Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
	}

	if (EnumHasAnyFlags(InUsage, BUF_DrawIndirect))
	{
		ResourceDesc.Flags |= D3D12RHI_RESOURCE_FLAG_ALLOW_INDIRECT_BUFFER;
	}

	if (EnumHasAnyFlags(InUsage, BUF_Shared))
	{
		ResourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
	}

	if (EnumHasAnyFlags(InUsage, BUF_ReservedResource))
	{
		checkf(InStride <= GRHIGlobals.ReservedResources.TileSizeInBytes,
			TEXT("Reserved buffer stride %d must not be greater than the reserved resource tile size %d"), 
			InStride, GRHIGlobals.ReservedResources.TileSizeInBytes);

		Alignment = GRHIGlobals.ReservedResources.TileSizeInBytes;
	}
	else
	{
		// Structured buffers, non-ByteAddress buffers, need to be aligned to their stride to ensure that they can be addressed correctly with element based offsets.
		Alignment = (InStride > 0) && (EnumHasAnyFlags(InUsage, BUF_StructuredBuffer) || !EnumHasAnyFlags(InUsage, BUF_ByteAddressBuffer | BUF_DrawIndirect)) ? FMath::LeastCommonMultiplier(InStride, 16) : 16;
	}
}

FBufferRHIRef FD3D12DynamicRHI::RHICreateBuffer(FRHICommandListBase& RHICmdList, FRHIBufferDesc const& Desc, ERHIAccess ResourceState, FRHIResourceCreateInfo& CreateInfo)
{
	return CreateBuffer(RHICmdList, Desc, ResourceState, CreateInfo);
}

FBufferRHIRef FD3D12DynamicRHI::CreateBuffer(FRHICommandListBase& RHICmdList, FRHIBufferDesc const& BufferDesc, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo)
{
	if (BufferDesc.IsNull())
	{
		return GetAdapter().CreateLinkedObject<FD3D12Buffer>(CreateInfo.GPUMask, [BufferDesc](FD3D12Device* Device, FD3D12Buffer* FirstLinkedObject)
		{
			return new FD3D12Buffer(Device, BufferDesc);
		});
	}

	return CreateD3D12Buffer(&RHICmdList, BufferDesc, InResourceState, CreateInfo);
}

FD3D12Buffer* FD3D12DynamicRHI::CreateD3D12Buffer(class FRHICommandListBase* RHICmdList, FRHIBufferDesc const& BufferDesc, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo, ID3D12ResourceAllocator* ResourceAllocator, bool bForceKeepUnlocked)
{
	FName TraceClassName = GetRHIBufferClassName(CreateInfo.GetTraceClassName());

	D3D12_RESOURCE_DESC Desc;
	uint32 Alignment;
	FD3D12Buffer::GetResourceDescAndAlignment(BufferDesc.Size, BufferDesc.Stride, BufferDesc.Usage, Desc, Alignment);

	ED3D12ResourceStateMode StateMode = EnumHasAllFlags(BufferDesc.Usage, BUF_AccelerationStructure)
		? ED3D12ResourceStateMode::SingleState 
		: ED3D12ResourceStateMode::Default;

	const bool bHasInitialData = CreateInfo.ResourceArray != nullptr;

	const bool bIsDynamic = EnumHasAnyFlags(BufferDesc.Usage, BUF_AnyDynamic);

	if (EnumHasAnyFlags(BufferDesc.Usage, BUF_ReservedResource))
	{
		checkf(!bHasInitialData, TEXT("Reserved resources may not have initial data"));
		checkf(!bIsDynamic, TEXT("Reserved resources may not be dynamic"));
		checkf(!ResourceAllocator, TEXT("Reserved resources may not use a custom resource allocator"));
	}

	D3D12_HEAP_TYPE HeapType = bIsDynamic ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_DEFAULT;
	const FD3D12Resource::FD3D12ResourceTypeHelper Type(Desc, HeapType);

	// Does this resource support tracking?
	const bool bSupportResourceStateTracking = !bIsDynamic && FD3D12DefaultBufferAllocator::IsPlacedResource(Desc.Flags, StateMode, Alignment) && Type.bWritable;

	// Initial state is derived from the InResourceState if it supports tracking
	D3D12_RESOURCE_STATES DesiredState = bSupportResourceStateTracking ? Type.GetOptimalInitialState(InResourceState, false) :
		FD3D12DefaultBufferAllocator::GetDefaultInitialResourceState(HeapType, BufferDesc.Usage, StateMode);

	// Setup the state at which the resource needs to be created - copy dest only supported for placed resources
	D3D12_RESOURCE_STATES CreateState = (CreateInfo.ResourceArray && bSupportResourceStateTracking) ? D3D12_RESOURCE_STATE_COPY_DEST : DesiredState;

	// Theoretically, we could assert if GPUMask isn't correct, but at the moment the RDG and RHI buffer descriptions don't include the
	// GPU mask, so there's no way for the caller to configure it (only the lower level CreateInfo includes it).  Note that differentiation
	// for NNE (DirectML) is required beyond just setting the mask anyway, in the sense of forcing separate GPU0 visible only heaps, not just
	// filtering which GPU copies are allocated.  Because this is necessary to solve a crash, it's higher priority than GPUMask support,
	// which may be added in the future.
	FRHIGPUMask GPUMask = EnumHasAnyFlags(BufferDesc.Usage, EBufferUsageFlags::NNE) ? FRHIGPUMask::GPU0() : CreateInfo.GPUMask;

	FD3D12Buffer* Buffer = GetAdapter().CreateRHIBuffer(Desc, Alignment, BufferDesc, StateMode, CreateState, bHasInitialData || bForceKeepUnlocked, GPUMask, ResourceAllocator, CreateInfo.DebugName, CreateInfo.OwnerName, TraceClassName);
	check(Buffer->ResourceLocation.IsValid());

	// Copy the resource data if available 
	if (bHasInitialData)
	{
		check(RHICmdList);
		Buffer->UploadResourceData(*RHICmdList, CreateInfo.ResourceArray, DesiredState, CreateInfo.DebugName, TraceClassName, CreateInfo.OwnerName);
	}

	return Buffer;
}

FRHIBuffer* FD3D12DynamicRHI::CreateBuffer(const FRHIBufferCreateInfo& CreateInfo, const TCHAR* DebugName, ERHIAccess InitialState, ID3D12ResourceAllocator* ResourceAllocator)
{
	FRHIResourceCreateInfo ResourceCreateInfo(DebugName);
	return CreateD3D12Buffer(nullptr, FRHIBufferDesc(CreateInfo.Size, CreateInfo.Stride, CreateInfo.Usage), InitialState, ResourceCreateInfo, ResourceAllocator);
}

void* FD3D12DynamicRHI::LockBuffer(FRHICommandListBase& RHICmdList, FD3D12Buffer* Buffer, uint32 BufferSize, EBufferUsageFlags BufferUsage, uint32 Offset, uint32 Size, EResourceLockMode LockMode)
{
	SCOPE_CYCLE_COUNTER(STAT_D3D12LockBufferTime);
	UE_TRACE_METADATA_SCOPE_ASSET_FNAME(Buffer->GetName(), Buffer->GetName(), Buffer->GetOwnerName());

	checkf(Size <= BufferSize, TEXT("Requested lock size %u is larger than the total size %u for buffer '%s'."), Size, BufferSize, *Buffer->GetName().ToString());

	FD3D12LockedResource& LockedData = Buffer->LockedData;
	check(LockedData.bLocked == false);

	FD3D12Adapter& Adapter = GetAdapter();

	void* Data = nullptr;

	// Determine whether the buffer is dynamic or not.
	if (EnumHasAnyFlags(BufferUsage, BUF_AnyDynamic))
	{
		check(LockMode == RLM_WriteOnly || LockMode == RLM_WriteOnly_NoOverwrite);

		if (LockedData.bHasNeverBeenLocked || LockMode == RLM_WriteOnly_NoOverwrite)
		{
			// Buffers on upload heap are mapped right after creation
			Data = Buffer->ResourceLocation.GetMappedBaseAddress();
			check(!!Data);
		}
		else
		{
			FD3D12Device* Device = Buffer->GetParentDevice();

			FD3D12ResourceLocation NewLocation(Device);
			Data = Adapter.GetUploadHeapAllocator(Device->GetGPUIndex()).AllocUploadResource(BufferSize, Buffer->BufferAlignment, NewLocation);

			RHICmdList.EnqueueLambdaMultiPipe(GetEnabledRHIPipelines(), FRHICommandListBase::EThreadFence::Enabled, TEXT("FD3D12DynamicRHI::LockBuffer"),
			[
				Resource = Buffer,
				NewLocation = MoveTemp(NewLocation)
			](FD3D12ContextArray const& Contexts) mutable
			{
				const static FLazyName ExecuteName(TEXT("FRHICommandRenameUploadBuffer::Execute"));
				UE_TRACE_METADATA_SCOPE_ASSET_FNAME(Resource->GetName(), ExecuteName, Resource->GetOwnerName());

				for (FD3D12Buffer& DeviceBuffer : *Resource)
				{
					for (FD3D12CommandContextBase* ContextBase : Contexts)
					{
						if (FD3D12CommandContext* Context = ContextBase ? ContextBase->GetSingleDeviceContext(DeviceBuffer.GetParentDevice()->GetGPUIndex()) : nullptr)
						{
							// Clear the resource if still bound to make sure the SRVs are rebound again on next operation. This needs to happen
							// on the RHI timeline when this command runs at the top of the pipe (which can happen when locking buffers in
							// RLM_WriteOnly_NoOverwrite mode).
							Context->ConditionalClearShaderResource(&DeviceBuffer.ResourceLocation, EShaderParameterTypeMask::SRVMask);
						}
					}
				}

#if UE_MEMORY_TRACE_ENABLED
				// This memory trace happens before RenameLDAChain so the old & new GPU addresses are correct
				MemoryTrace_ReallocFree(Resource->ResourceLocation.GetGPUVirtualAddress(), EMemoryTraceRootHeap::VideoMemory);
				MemoryTrace_ReallocAlloc(NewLocation.GetGPUVirtualAddress(), Resource->ResourceLocation.GetSize(), Resource->BufferAlignment, EMemoryTraceRootHeap::VideoMemory);
#endif
				Resource->RenameLDAChain(Contexts, NewLocation);
			});
		}
	}
	else
	{
		// Static and read only buffers only have one version of the content. Use the first related device.
		FD3D12Device* Device = Buffer->GetParentDevice();
		FD3D12Resource* pResource = Buffer->ResourceLocation.GetResource();

		// Locking for read must occur immediately so we can't queue up the operations later.
		if (LockMode == RLM_ReadOnly)
		{
			LockedData.bLockedForReadOnly = true;
			// If the static buffer is being locked for reading, create a staging buffer.
			FD3D12Resource* StagingBuffer = nullptr;

			const FRHIGPUMask Node = Device->GetGPUMask();
			VERIFYD3D12RESULT(Adapter.CreateBuffer(D3D12_HEAP_TYPE_READBACK, Node, Node, Offset + Size, &StagingBuffer, nullptr));

			// Copy the contents of the buffer to the staging buffer.
			RHICmdList.EnqueueLambda([Node, StagingBuffer, pResource, Buffer, Offset, Size](FRHICommandListBase& ExecutingCmdList)
			{
				FD3D12CommandContext& Context = FD3D12CommandContext::Get(ExecutingCmdList, Node.GetFirstIndex());
				uint64 SubAllocOffset = Buffer->ResourceLocation.GetOffsetFromBaseOfResource();

				FScopedResourceBarrier ScopeResourceBarrierSource(Context, pResource, &Buffer->ResourceLocation, D3D12_RESOURCE_STATE_COPY_SOURCE, 0);
				// Don't need to transition upload heaps
				Context.FlushResourceBarriers(); // Must flush so the desired state is actually set.

				Context.UpdateResidency(StagingBuffer);
				Context.UpdateResidency(pResource);

				Context.GraphicsCommandList()->CopyBufferRegion(
					StagingBuffer->GetResource(),
					0,
					pResource->GetResource(),
					SubAllocOffset + Offset, Size);
			});
			
			RHICmdList.GetAsImmediate().SubmitAndBlockUntilGPUIdle();

			LockedData.ResourceLocation.AsStandAlone(StagingBuffer, Size);
			Data = LockedData.ResourceLocation.GetMappedBaseAddress();
		}
		else
		{
			// If the static buffer is being locked for writing, allocate memory for the contents to be written to.
			Data = Adapter.GetUploadHeapAllocator(Device->GetGPUIndex()).AllocUploadResource(Size, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT, LockedData.ResourceLocation);
		}
	}

	LockedData.LockOffset = Offset;
	LockedData.LockSize = Size;
	LockedData.bLocked = true;
	LockedData.bHasNeverBeenLocked = false;

	// Return the offset pointer
	check(Data != nullptr);
	return Data;
}

void FD3D12DynamicRHI::UnlockBuffer(FRHICommandListBase& RHICmdList, FD3D12Buffer* Buffer, EBufferUsageFlags BufferUsage)
{
	SCOPE_CYCLE_COUNTER(STAT_D3D12UnlockBufferTime);

	FD3D12LockedResource& LockedData = Buffer->LockedData;
	check(LockedData.bLocked == true);

	// Determine whether the buffer is dynamic or not.
	if (EnumHasAnyFlags(BufferUsage, BUF_AnyDynamic))
	{
		// If the Buffer is dynamic, its upload heap memory can always stay mapped. Don't do anything.
	}
	else if (LockedData.bLockedForReadOnly)
	{
		// Nothing to do, just release the locked data at the end of the function
	}
	else
	{
		// Update all of the resources in the LDA chain
		check(Buffer->IsHeadLink());

		RHICmdList.EnqueueLambda([
			RootBuffer = Buffer,
			LockedData = MoveTemp(LockedData)
		](FRHICommandListBase& ExecutingCmdList)
		{
			for (FD3D12Buffer& Buffer : *RootBuffer)
			{
				FD3D12CommandContext& Context = FD3D12CommandContext::Get(ExecutingCmdList, Buffer.GetParentDevice()->GetGPUIndex());

				FD3D12Resource* SourceResource = LockedData.ResourceLocation.GetResource();
				uint32 SourceFullOffset = LockedData.ResourceLocation.GetOffsetFromBaseOfResource();

				FD3D12Resource* DestResource = Buffer.ResourceLocation.GetResource();
				uint32 DestFullOffset = Buffer.ResourceLocation.GetOffsetFromBaseOfResource() + LockedData.LockOffset;

				// Clear the resource if still bound to make sure the SRVs are rebound again on next operation (and get correct resource transitions enqueued)
				Context.ConditionalClearShaderResource(&Buffer.ResourceLocation, EShaderParameterTypeMask::SRVMask);

				FScopedResourceBarrier ScopeResourceBarrierDest(Context, DestResource, &Buffer.ResourceLocation, D3D12_RESOURCE_STATE_COPY_DEST, 0);
				// Don't need to transition upload heaps
				Context.FlushResourceBarriers();

				Context.UpdateResidency(DestResource);
				Context.UpdateResidency(SourceResource);

				Context.GraphicsCommandList()->CopyBufferRegion(
					DestResource->GetResource(),
					DestFullOffset,
					SourceResource->GetResource(),
					SourceFullOffset,
					LockedData.LockSize
				);
				
				Context.ConditionalSplitCommandList();

				DEBUG_RHI_EXECUTE_COMMAND_LIST(this);
			}
		});
	}

	LockedData.Reset();
}

void* FD3D12DynamicRHI::RHILockBuffer(FRHICommandListBase& RHICmdList, FRHIBuffer* BufferRHI, uint32 Offset, uint32 Size, EResourceLockMode LockMode)
{
	// If you hit this assert, you should be using LockBufferMGPU and iterating over FRHIGPUMask::All() to initialize the resource separately for each GPU.
	// "MultiGPUAllocate" only makes sense if a buffer must vary per GPU, for example if it's a buffer that includes GPU specific virtual addresses for ray
	// tracing acceleration structures.
	check(!EnumHasAnyFlags(BufferRHI->GetUsage(), BUF_MultiGPUAllocate));

	FD3D12Buffer* Buffer = FD3D12DynamicRHI::ResourceCast(BufferRHI);
	return LockBuffer(RHICmdList, Buffer, Buffer->GetSize(), Buffer->GetUsage(), Offset, Size, LockMode);
}

void* FD3D12DynamicRHI::RHILockBufferMGPU(FRHICommandListBase& RHICmdList, FRHIBuffer* BufferRHI, uint32 GPUIndex, uint32 Offset, uint32 Size, EResourceLockMode LockMode)
{
	// If you hit this assert, you should be using LockBuffer to initialize the resource, rather than this function.  The MGPU version is only for resources
	// with the MultiGPUAllocate flag, where it's necessary for the caller to initialize the buffer for each GPU.  The other LockBuffer call initializes the
	// resource on all GPUs with one call, due to driver mirroring of the underlying resource.
	check(EnumHasAnyFlags(BufferRHI->GetUsage(), BUF_MultiGPUAllocate));

	FD3D12Buffer* Buffer = FD3D12DynamicRHI::ResourceCast(BufferRHI, GPUIndex);
	return LockBuffer(RHICmdList, Buffer, Buffer->GetSize(), Buffer->GetUsage(), Offset, Size, LockMode);
}

void FD3D12DynamicRHI::RHIUnlockBuffer(FRHICommandListBase& RHICmdList, FRHIBuffer* BufferRHI)
{
	check(!EnumHasAnyFlags(BufferRHI->GetUsage(), BUF_MultiGPUAllocate));

	FD3D12Buffer* Buffer = FD3D12DynamicRHI::ResourceCast(BufferRHI);
	UnlockBuffer(RHICmdList, Buffer, Buffer->GetUsage());
}

void FD3D12DynamicRHI::RHIUnlockBufferMGPU(FRHICommandListBase& RHICmdList, FRHIBuffer* BufferRHI, uint32 GPUIndex)
{
	check(EnumHasAnyFlags(BufferRHI->GetUsage(), BUF_MultiGPUAllocate));

	FD3D12Buffer* Buffer = FD3D12DynamicRHI::ResourceCast(BufferRHI, GPUIndex);
	UnlockBuffer(RHICmdList, Buffer, Buffer->GetUsage());
}

void FD3D12DynamicRHI::RHIBindDebugLabelName(FRHICommandListBase& RHICmdList, FRHIBuffer* BufferRHI, const TCHAR* Name)
{
	if (BufferRHI == nullptr || !GD3D12BindResourceLabels)
	{
		return;
	}

#if NAME_OBJECTS
	FD3D12Buffer* Buffer = FD3D12DynamicRHI::ResourceCast(BufferRHI);

	// only rename the underlying d3d12 resource if it's not sub allocated (requires resource state tracking or stand alone allocated)
	if (Buffer->GetResource() != nullptr && (Buffer->GetResource()->RequiresResourceStateTracking() || Buffer->ResourceLocation.GetType() == FD3D12ResourceLocation::ResourceLocationType::eStandAlone))
	{
		FD3D12Buffer::FLinkedObjectIterator BufferIt(Buffer);

		if (GNumExplicitGPUsForRendering > 1)
		{
			// Generate string of the form "Name (GPU #)" -- assumes GPU index is a single digit.  This is called many times
			// a frame, so we want to avoid any string functions which dynamically allocate, to reduce perf overhead.
			static_assert(MAX_NUM_GPUS <= 10);

			static const TCHAR NameSuffix[] = TEXT(" (GPU #)");
			constexpr int32 NameSuffixLengthWithTerminator = (int32)UE_ARRAY_COUNT(NameSuffix);
			constexpr int32 NameBufferLength = 256;
			constexpr int32 GPUIndexSuffixOffset = 6;		// Offset of '#' character

			// Combine Name and suffix in our string buffer (clamping the length for bounds checking).  We'll replace the GPU index
			// with the appropriate digit in the loop.
			int32 NameLength = FMath::Min(FCString::Strlen(Name), NameBufferLength - NameSuffixLengthWithTerminator);
			int32 GPUIndexOffset = NameLength + GPUIndexSuffixOffset;

			TCHAR DebugName[NameBufferLength];
			FMemory::Memcpy(&DebugName[0], Name, NameLength * sizeof(TCHAR));
			FMemory::Memcpy(&DebugName[NameLength], NameSuffix, NameSuffixLengthWithTerminator * sizeof(TCHAR));

			for (; BufferIt; ++BufferIt)
			{
				FD3D12Resource* Resource = BufferIt->GetResource();

				DebugName[GPUIndexOffset] = TEXT('0') + BufferIt->GetParentDevice()->GetGPUIndex();

				SetName(Resource, DebugName);
			}
		}
		else
		{
			SetName(Buffer->GetResource(), Name);
		}
	}
#endif

	// Also set on RHI object
	BufferRHI->SetName(Name);
}

void FD3D12CommandContext::RHICopyBufferRegion(FRHIBuffer* DestBufferRHI, uint64 DstOffset, FRHIBuffer* SourceBufferRHI, uint64 SrcOffset, uint64 NumBytes)
{
	FD3D12Buffer* SourceBuffer = RetrieveObject<FD3D12Buffer>(SourceBufferRHI);
	FD3D12Buffer* DestBuffer = RetrieveObject<FD3D12Buffer>(DestBufferRHI);

	FD3D12Device* BufferDevice = SourceBuffer->GetParentDevice();
	check(BufferDevice == DestBuffer->GetParentDevice());
	check(BufferDevice == GetParentDevice());

	FD3D12Resource* pSourceResource = SourceBuffer->ResourceLocation.GetResource();
	D3D12_RESOURCE_DESC const& SourceBufferDesc = pSourceResource->GetDesc();

	FD3D12Resource* pDestResource = DestBuffer->ResourceLocation.GetResource();
	D3D12_RESOURCE_DESC const& DestBufferDesc = pDestResource->GetDesc();

	checkf(pSourceResource != pDestResource, TEXT("CopyBufferRegion cannot be used on the same resource. This can happen when both the source and the dest are suballocated from the same resource."));

	check(DstOffset + NumBytes <= DestBufferDesc.Width);
	check(SrcOffset + NumBytes <= SourceBufferDesc.Width);

	FScopedResourceBarrier ScopeResourceBarrierSrc(*this, pSourceResource, &SourceBuffer->ResourceLocation, D3D12_RESOURCE_STATE_COPY_SOURCE, 0);
	FScopedResourceBarrier ScopeResourceBarrierDst(*this, pDestResource  , &DestBuffer->ResourceLocation, D3D12_RESOURCE_STATE_COPY_DEST  , 0);
	FlushResourceBarriers();

	GraphicsCommandList()->CopyBufferRegion(pDestResource->GetResource(), DestBuffer->ResourceLocation.GetOffsetFromBaseOfResource() + DstOffset, pSourceResource->GetResource(), SourceBuffer->ResourceLocation.GetOffsetFromBaseOfResource() + SrcOffset, NumBytes);
	UpdateResidency(pDestResource);
	UpdateResidency(pSourceResource);

	ConditionalSplitCommandList();

	BufferDevice->RegisterGPUWork(1);
}
