// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetalBindlessDescriptors.h"
#include "GlobalShader.h"
#include "PipelineStateCache.h"
#include "MetalRHIContext.h"
#include "UpdateDescriptorHandle.h"

#if PLATFORM_SUPPORTS_BINDLESS_RENDERING

#include "MetalDevice.h"
#include "MetalCommandEncoder.h"
#include "MetalDynamicRHI.h"

#define USE_CPU_DESCRIPTOR_COPY 0
#define USE_DESCRIPTOR_BUFFER_COPY !USE_CPU_DESCRIPTOR_COPY

int32 GBindlessResourceDescriptorHeapSize = 2048 * 1024;
static FAutoConsoleVariableRef CVarBindlessResourceDescriptorHeapSize(
	TEXT("Metal.Bindless.ResourceDescriptorHeapSize"),
	GBindlessResourceDescriptorHeapSize,
	TEXT("Bindless resource descriptor heap size"),
	ECVF_ReadOnly
);

int32 GBindlessSamplerDescriptorHeapSize = 64 << 10; // TODO: We should be able to reduce the size of the sampler heap if we fix static sampler creation.
static FAutoConsoleVariableRef CVarBindlessSamplerDescriptorHeapSize(
	TEXT("Metal.Bindless.SamplerDescriptorHeapSize"),
	GBindlessSamplerDescriptorHeapSize,
	TEXT("Bindless sampler descriptor heap size"),
	ECVF_ReadOnly
);

FMetalDescriptorHeap::FMetalDescriptorHeap(FMetalDevice& MetalDevice, const ERHIDescriptorHeapType DescriptorType)
	: Device(MetalDevice)
	, ResourceHeap(nullptr)
	, Type(DescriptorType)
{
}

void FMetalDescriptorHeap::Init(const int32 HeapSize)
{
	FRHICommandListImmediate& RHICmdList = FRHICommandListImmediate::Get();
	
	FRHIBufferDesc Desc(HeapSize, 1, BUF_Dynamic | BUF_KeepCPUAccessible | BUF_StructuredBuffer | BUF_UnorderedAccess);
	FRHIResourceCreateInfo CreateInfo(TEXT("ResourceHeap"));
	
	ResourceHeapLength = HeapSize;
	ResourceHeap = new FMetalRHIBuffer(RHICmdList, Device, Desc, CreateInfo);
	
	FMetalRHIBuffer* Buffer = ResourceCast(ResourceHeap.GetReference());
	
	if(Type == ERHIDescriptorHeapType::Sampler)
	{
		Descriptors = reinterpret_cast<IRDescriptorTableEntry*>(Buffer->GetCurrentBuffer()->Contents());
	}
	else
	{
#if !USE_DESCRIPTOR_BUFFER_COPY
		Descriptors = reinterpret_cast<IRDescriptorTableEntry*>(Buffer->GetCurrentBuffer()->Contents());
#else
		Descriptors = reinterpret_cast<IRDescriptorTableEntry*>(FMemory::Malloc(HeapSize, 16));
#endif
	}
	
	DescriptorsDirty = false;
	MinDirtyIndex = UINT32_MAX;
	MaxDirtyIndex = 0;
}

void FMetalDescriptorHeap::FreeDescriptor(FRHIDescriptorHandle DescriptorHandle)
{
	FScopeLock ScopeLock(&FreeListCS);
	FreeList.Enqueue(DescriptorHandle.GetIndex());
}

uint32 FMetalDescriptorHeap::GetFreeResourceIndex()
{
	{
		FScopeLock ScopeLock(&FreeListCS);
		if (!FreeList.IsEmpty())
		{
			uint32 FreeIndex;
			FreeList.Dequeue(FreeIndex);
			return FreeIndex;
		}
	}

	NSUInteger MaxDescriptorCount = ResourceHeapLength / sizeof(IRDescriptorTableEntry);
	checkf((PeakDescriptorCount + 1) < MaxDescriptorCount, TEXT("Reached Heap Max Capacity (%u/%u)"), PeakDescriptorCount + 1, MaxDescriptorCount);

	const uint32 ResourceIndex = PeakDescriptorCount++;
	return ResourceIndex;
}

FRHIDescriptorHandle FMetalDescriptorHeap::ReserveDescriptor()
{
	const uint32 ResourceIndex = GetFreeResourceIndex();
	return FRHIDescriptorHandle(Type, ResourceIndex);
}

void FMetalDescriptorHeap::UpdateDescriptor(FRHIDescriptorHandle DescriptorHandle, IRDescriptorTableEntry DescriptorData)
{
	checkf(DescriptorHandle.IsValid(), TEXT("Attemping to update invalid descriptor handle!"));

	uint32 DescriptorIndex = DescriptorHandle.GetIndex();
	Descriptors[DescriptorIndex] = DescriptorData;
	
	DescriptorsDirty = true;
	MinDirtyIndex = FMath::Min(DescriptorIndex, MinDirtyIndex);
	MaxDirtyIndex = FMath::Max(DescriptorIndex, MaxDirtyIndex);
}

void FMetalDescriptorHeap::BindHeap(FMetalCommandEncoder* Encoder, MTL::FunctionType FunctionType, const uint32 BindIndex)
{
	uint32 DescriptorCount = PeakDescriptorCount.load();
	const uint64 HeapSize = DescriptorCount * sizeof(IRDescriptorTableEntry);

	FMetalRHIBuffer* Buffer = ResourceCast(ResourceHeap.GetReference());
	Encoder->SetShaderBuffer(FunctionType, Buffer->GetCurrentBuffer(), 0, HeapSize, BindIndex, MTL::ResourceUsageRead);
}

FMetalBindlessDescriptorManager::FMetalBindlessDescriptorManager(FMetalDevice& MetalDevice)
	: Device(MetalDevice)
	, StandardResources(Device, ERHIDescriptorHeapType::Standard)
	, SamplerResources(Device, ERHIDescriptorHeapType::Sampler)
{
	
}

FMetalBindlessDescriptorManager::~FMetalBindlessDescriptorManager()
{

}

void FMetalBindlessDescriptorManager::Init()
{
	StandardResources.Init(GBindlessResourceDescriptorHeapSize);
	SamplerResources.Init(GBindlessSamplerDescriptorHeapSize);
	
	bIsSupported = true;
}

FRHIDescriptorHandle FMetalBindlessDescriptorManager::ReserveDescriptor(ERHIDescriptorHeapType InType)
{
	switch (InType)
	{
	case ERHIDescriptorHeapType::Standard:
		return StandardResources.ReserveDescriptor();
	case ERHIDescriptorHeapType::Sampler:
		return SamplerResources.ReserveDescriptor();
	default:
		checkNoEntry();
	};

	return FRHIDescriptorHandle();
}

void FMetalBindlessDescriptorManager::FreeDescriptor(FRHIDescriptorHandle DescriptorHandle)
{
	check(DescriptorHandle.IsValid());
	switch (DescriptorHandle.GetType())
	{
	case ERHIDescriptorHeapType::Standard:
		StandardResources.FreeDescriptor(DescriptorHandle);
		break;
	case ERHIDescriptorHeapType::Sampler:
		SamplerResources.FreeDescriptor(DescriptorHandle);
		break;
	default:
		checkNoEntry();
	};
}

void FMetalBindlessDescriptorManager::BindSampler(FRHIDescriptorHandle DescriptorHandle, MTL::SamplerState* Sampler)
{
	IRDescriptorTableEntry DescriptorData = {0};
	IRDescriptorTableSetSampler(&DescriptorData, Sampler, 0.0f);

	SamplerResources.UpdateDescriptor(DescriptorHandle, DescriptorData);
}

void FMetalBindlessDescriptorManager::BindResource(FRHIDescriptorHandle DescriptorHandle, FMetalResourceViewBase* Resource)
{
	IRDescriptorTableEntry DescriptorData = {0};

	switch (Resource->GetMetalType())
	{
	case FMetalResourceViewBase::EMetalType::TextureView:
		{
			auto const& View = Resource->GetTextureView();

			IRDescriptorTableSetTexture(&DescriptorData, View.get(), 0.0f, 0u);
		}
		break;
	case FMetalResourceViewBase::EMetalType::BufferView:
		{
			auto const& View = Resource->GetBufferView();

			IRDescriptorTableSetBuffer(&DescriptorData, View.Buffer->GetGPUAddress() + View.Offset, View.Size);
		}
		break;
	case FMetalResourceViewBase::EMetalType::TextureBufferBacked:
		{
			auto const& View = Resource->GetTextureBufferBacked();

			IRBufferView BufferView;
			BufferView.buffer = View.Buffer->GetMTLBuffer();
			BufferView.bufferOffset = View.Buffer->GetOffset() + View.Offset;
			BufferView.bufferSize = View.Size;
			BufferView.typedBuffer = true;
			BufferView.textureBufferView = View.Texture.get();

			uint32 Stride = GPixelFormats[View.Format].BlockBytes;
			uint32 FirstElement = View.Offset / Stride;
			uint32 NumElement = View.Size / Stride;

			uint64 BufferVA              = View.Buffer->GetGPUAddress() + View.Offset;
			uint64_t ExtraElement        = (BufferVA % 16) / Stride;

			BufferView.textureViewOffsetInElements = ExtraElement;

			IRDescriptorTableSetBufferView(&DescriptorData, &BufferView);
		}
		break;
#if METAL_RHI_RAYTRACING
	case FMetalResourceViewBase::EMetalType::AccelerationStructure:
		{
			MTL::AccelerationStructure const& AccelerationStructure = Resource->GetAccelerationStructure();

			IRDescriptorTableSetAccelerationStructure(&DescriptorData, [AccelerationStructure.GetPtr() gpuResourceID]._impl);
		}
		break;
#endif
	default:
		checkNoEntry();
		return;
	};

#if !USE_DESCRIPTOR_BUFFER_COPY && !USE_CPU_DESCRIPTOR_COPY
	if(GIsRHIInitialized)
	{
		FScopeLock ScopeLock(&ComputeDescriptorCS);
		
		StandardResources.ComputeDescriptorEntries.Add(DescriptorData);
		StandardResources.ComputeDescriptorIndices.Add(DescriptorHandle.GetIndex());
	}
	else
	{
		StandardResources.UpdateDescriptor(DescriptorHandle, DescriptorData);
	}
#else
	StandardResources.UpdateDescriptor(DescriptorHandle, DescriptorData);
#endif
}

void FMetalBindlessDescriptorManager::UpdateDescriptorsWithGPU(FMetalRHICommandContext* Context)
{
#if USE_CPU_DESCRIPTOR_COPY
	return;
#elif USE_DESCRIPTOR_BUFFER_COPY
	UpdateDescriptorsWithCopy(Context);
#else
	UpdateDescriptorsWithCompute();
#endif
}

void FMetalBindlessDescriptorManager::UpdateDescriptorsWithCopy(FMetalRHICommandContext* Context)
{
#if !USE_CPU_DESCRIPTOR_COPY
	FScopeLock ScopeLock(&ComputeDescriptorCS);
	
	if(!StandardResources.DescriptorsDirty)
	{
		return;
	}
	
	uint32_t IndexOffset = StandardResources.MinDirtyIndex;
	uint32_t UpdateSize = ((StandardResources.MaxDirtyIndex - StandardResources.MinDirtyIndex) + 1) * sizeof(IRDescriptorTableEntry);
	uint32_t UpdateOffset = StandardResources.MinDirtyIndex * sizeof(IRDescriptorTableEntry);
	
	FMetalBufferPtr SourceBuffer = Device.GetTransferAllocator()->Allocate(UpdateSize);
	
	StandardResources.DescriptorsDirty = false;
	StandardResources.MinDirtyIndex = UINT32_MAX;
	StandardResources.MaxDirtyIndex = 0;
	
	FMetalRHIBuffer* DestBuffer = ResourceCast(StandardResources.ResourceHeap.GetReference());
	
	IRDescriptorTableEntry* DescriptorCopy = (IRDescriptorTableEntry*)SourceBuffer->Contents();
	
#if USE_DESCRIPTOR_BUFFER_COPY
	FMemory::Memcpy(DescriptorCopy, StandardResources.Descriptors + IndexOffset, UpdateSize);
#else
	FMemory::Memcpy(DescriptorCopy, StandardResources.Descriptors + IndexOffset, StandardResources.ResourceHeapLength);
	
	for(uint32_t Idx = 0; Idx < ComputeDescriptorEntriesCopy.Num(); ++Idx)
	{
		DescriptorCopy[ComputeDescriptorIndicesCopy[Idx]-IndexOffset] = ComputeDescriptorEntriesCopy[Idx];
	}
#endif
	Context->CopyFromBufferToBuffer(SourceBuffer, 0, DestBuffer->GetCurrentBuffer(), UpdateOffset, UpdateSize);
}

void FMetalBindlessDescriptorManager::UpdateDescriptorsWithCompute()
{
	FScopeLock ScopeLock(&ComputeDescriptorCS);
	
	TResourceArray<IRDescriptorTableEntry> ComputeDescriptorEntriesCopy = MoveTemp(StandardResources.ComputeDescriptorEntries);
	TResourceArray<uint32_t> ComputeDescriptorIndicesCopy = MoveTemp(StandardResources.ComputeDescriptorIndices);
	
	uint32_t NumDescriptors = ComputeDescriptorIndicesCopy.Num();
	
	if(!NumDescriptors)
	{
		return;
	}
	
	FMetalRHICommandContext* Context = static_cast<FMetalRHICommandContext*>(RHIGetDefaultContext());
	TRHIComputeCommandList_RecursiveHazardous<FMetalRHICommandContext> RHICmdList(Context);

	TShaderMapRef<FUpdateDescriptorHandleCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	FRHIComputeShader* ShaderRHI = ComputeShader.GetComputeShader();
	SetComputePipelineState(RHICmdList, ShaderRHI);
	
	FShaderResourceViewRHIRef DescriptorEntriesView;
	FShaderResourceViewRHIRef DescriptorIndicesView;
	FUnorderedAccessViewRHIRef DstDescriptorBufferView;
	
	{
		FRHIResourceCreateInfo CreateInfo(TEXT("DescriptorEntries"), &ComputeDescriptorEntriesCopy);
		CreateInfo.GPUMask = FRHIGPUMask::GPU0();
		
		FBufferRHIRef DescriptorEntriesBuffer = RHICmdList.CreateStructuredBuffer(sizeof(IRDescriptorTableEntry), NumDescriptors * sizeof(IRDescriptorTableEntry), BUF_Dynamic | BUF_ShaderResource | BUF_KeepCPUAccessible, CreateInfo);
		
		auto DescriptorEntriesBufferCreateDesc = FRHIViewDesc::CreateBufferSRV()
			.SetType(FRHIViewDesc::EBufferType::Structured)
			.SetStride(sizeof(IRDescriptorTableEntry))
			.SetNumElements(NumDescriptors);
		
		DescriptorEntriesView = RHICmdList.CreateShaderResourceView(DescriptorEntriesBuffer, DescriptorEntriesBufferCreateDesc);
	}
	
	{
		FRHIResourceCreateInfo CreateInfo(TEXT("DescriptorIndices"), &ComputeDescriptorIndicesCopy);
		CreateInfo.GPUMask = FRHIGPUMask::GPU0();
		
		FBufferRHIRef DescriptorIndicesBuffer = RHICmdList.CreateStructuredBuffer(sizeof(uint), NumDescriptors*sizeof(uint), BUF_Dynamic | BUF_ShaderResource | BUF_KeepCPUAccessible, CreateInfo);
		
		auto DescriptorIndicesBufferCreateDesc = FRHIViewDesc::CreateBufferSRV()
			.SetType(FRHIViewDesc::EBufferType::Structured)
			.SetStride(sizeof(uint))
			.SetNumElements(NumDescriptors);
		
		DescriptorIndicesView = RHICmdList.CreateShaderResourceView(DescriptorIndicesBuffer, DescriptorIndicesBufferCreateDesc);
	}
	
	{
		auto DstDescriptorBufferCreateDesc = FRHIViewDesc::CreateBufferUAV()
			.SetType(FRHIViewDesc::EBufferType::Structured)
			.SetStride(sizeof(IRDescriptorTableEntry))
			.SetNumElements(GBindlessResourceDescriptorHeapSize/sizeof(IRDescriptorTableEntry));
		
		DstDescriptorBufferView = RHICmdList.CreateUnorderedAccessView(StandardResources.ResourceHeap, DstDescriptorBufferCreateDesc);
	}
	
	FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
	
	SetShaderValue(BatchedParameters, ComputeShader->NumUpdates, NumDescriptors);
	SetSRVParameter(BatchedParameters, ComputeShader->DescriptorEntries, DescriptorEntriesView);
	SetSRVParameter(BatchedParameters, ComputeShader->DescriptorIndices, DescriptorIndicesView);
	SetUAVParameter(BatchedParameters, ComputeShader->OutputData, DstDescriptorBufferView);
	
	MTLEventPtr Evt = Device.CreateEvent();
	
	RHICmdList.EnqueueLambda([InContext=Context, InEvt=Evt](FRHICommandListBase&)
	{
		InContext->SignalEvent(InEvt, 1);
	});
	
	RHICmdList.SetBatchedShaderParameters(ShaderRHI, BatchedParameters);
	RHICmdList.DispatchComputeShader(NumDescriptors, 1, 1);
	
	RHICmdList.EnqueueLambda([InContext=Context, InEvt=Evt](FRHICommandListBase&)
	{
		InContext->WaitForEvent(InEvt, 1);
	});
	
	FMetalDynamicRHI::Get().DeferredDelete([Evt](){});
#endif
}

void FMetalBindlessDescriptorManager::BindTexture(FRHICommandListBase& RHICmdList, FRHIDescriptorHandle DescriptorHandle, MTL::Texture* Texture, EDescriptorUpdateType UpdateType)
{
	IRDescriptorTableEntry DescriptorData = {0};
	IRDescriptorTableSetTexture(&DescriptorData, Texture, 0.0f, 0u);
	
#if USE_DESCRIPTOR_BUFFER_COPY || USE_CPU_DESCRIPTOR_COPY
	UpdateType = EDescriptorUpdateType_Immediate;
#else
	UpdateType = !GIsRHIInitialized ? EDescriptorUpdateType_Immediate : UpdateType;
#endif
	
	RHICmdList.EnqueueLambda([this, InUpdateType=UpdateType, Data=DescriptorData, Handle=DescriptorHandle](FRHICommandListBase&)
	{
		FScopeLock ScopeLock(&ComputeDescriptorCS);
		if(InUpdateType == EDescriptorUpdateType_Immediate)
		{
			StandardResources.UpdateDescriptor(Handle, Data);
		}
		else
		{
			StandardResources.ComputeDescriptorEntries.Add(Data);
			StandardResources.ComputeDescriptorIndices.Add(Handle.GetIndex());
		}
	});
}

void FMetalBindlessDescriptorManager::BindDescriptorHeapsToEncoder(FMetalCommandEncoder* Encoder, MTL::FunctionType FunctionType, EMetalShaderStages Frequency)
{
	StandardResources.BindHeap(Encoder, FunctionType, kIRStandardHeapBindPoint);
	SamplerResources.BindHeap(Encoder, FunctionType, kIRSamplerHeapBindPoint);
}

#endif //PLATFORM_SUPPORTS_BINDLESS_RENDERING
