// Copyright Epic Games, Inc. All Rights Reserved.

#include "D3D12WorkGraph.h"

#include "Async/ParallelFor.h"
#include "Containers/DynamicRHIResourceArray.h"
#include "D3D12ExplicitDescriptorCache.h"
#include "D3D12RHIPrivate.h"
#include "D3D12ResourceCollection.h"
#include "D3D12Shader.h"
#include "PipelineStateCache.h"
#include "ShaderBundles.h"

static bool GShaderBundleSkipDispatch = false;
static FAutoConsoleVariableRef CVarShaderBundleSkipDispatch(
	TEXT("wg.ShaderBundle.SkipDispatch"),
	GShaderBundleSkipDispatch,
	TEXT("Whether to dispatch the built shader bundle pipeline (for debugging)"),
	ECVF_RenderThreadSafe
);

FD3D12WorkGraphPipelineState::FD3D12WorkGraphPipelineState(FD3D12Device* Device, const FWorkGraphPipelineStateInitializer& Initializer)
{
#if D3D12_RHI_WORKGRAPHS

	ID3D12Device9* Device9 = (ID3D12Device9*)Device->GetDevice();

	Shader = (FD3D12WorkGraphShader*)Initializer.GetShader();
	TCHAR const* ProgramName = Initializer.GetProgramName().IsEmpty() ? TEXT("WorkGraphProgram") : *Initializer.GetProgramName();

	CD3DX12_STATE_OBJECT_DESC StateObjectDesc(D3D12_STATE_OBJECT_TYPE_EXECUTABLE);

	CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT* GlobalRootSignature = StateObjectDesc.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
	GlobalRootSignature->SetRootSignature(Shader->RootSignature->GetRootSignature());

	{
		CD3DX12_DXIL_LIBRARY_SUBOBJECT* Lib = StateObjectDesc.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
		CD3DX12_SHADER_BYTECODE LibCode(Shader->Code.GetData(), Shader->Code.Num());
		Lib->SetDXILLibrary(&LibCode);
	}

	const int32 ShaderBundleNum = Initializer.GetShaderBundleNodeTable().Num();
	const bool bIsShaderBundle = ShaderBundleNum > 0;

	RootArgStrideInBytes = 0;

	if (bIsShaderBundle)
	{
		for (int32 Index = 0; Index < ShaderBundleNum; ++Index)
		{
			FD3D12WorkGraphShader* NodeShader = (FD3D12WorkGraphShader*)Initializer.GetShaderBundleNodeTable()[Index];
			LocalNodeShaders.Add(NodeShader);
			if (NodeShader != nullptr)
			{
				CD3DX12_DXIL_LIBRARY_SUBOBJECT* Lib = StateObjectDesc.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
				CD3DX12_SHADER_BYTECODE LibCode(NodeShader->Code.GetData(), NodeShader->Code.Num());
				Lib->SetDXILLibrary(&LibCode);

				FString NodeName = FString::Printf(TEXT("%s_%d"), *Initializer.GetShaderBundleNodeName(), Index);
				Lib->DefineExport(*NodeName, *NodeShader->EntryPoint);

				CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT* LocalRootSignature = StateObjectDesc.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
				LocalRootSignature->SetRootSignature(NodeShader->RootSignature->GetRootSignature());
				CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT* Association = StateObjectDesc.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
				Association->SetSubobjectToAssociate(*LocalRootSignature);
				Association->AddExport(*NodeName);

				RootArgStrideInBytes = FMath::Max(RootArgStrideInBytes, NodeShader->RootSignature->GetTotalRootSignatureSizeInBytes());
			}
		}
		RootArgStrideInBytes = ((RootArgStrideInBytes + 15) & ~15);
	}

	CD3DX12_WORK_GRAPH_SUBOBJECT* WorkGraph = StateObjectDesc.CreateSubobject<CD3DX12_WORK_GRAPH_SUBOBJECT>();
	WorkGraph->SetProgramName(ProgramName);
	
	D3D12_NODE_ID EntryPoint;
	EntryPoint.Name = *Shader->EntryPoint;
	EntryPoint.ArrayIndex = 0;
	WorkGraph->AddEntrypoint(EntryPoint);

	if (bIsShaderBundle)
	{
		for (int32 Index = 0; Index < ShaderBundleNum; ++Index)
		{
			if (LocalNodeShaders[Index])
			{
				FString NodeName = FString::Printf(TEXT("%s_%d"), *Initializer.GetShaderBundleNodeName(), Index);
				CD3DX12_COMMON_COMPUTE_NODE_OVERRIDES* Override = WorkGraph->CreateCommonComputeNodeOverrides(*NodeName);
				Override->NewName(D3D12_NODE_ID{ *Initializer.GetShaderBundleNodeName(), (uint32)Index });
			}
		}
	}

	WorkGraph->Finalize();

	HRESULT HResult = Device9->CreateStateObject(StateObjectDesc, IID_PPV_ARGS(StateObject.GetInitReference()));
	checkf(SUCCEEDED(HResult), TEXT("Failed to create work graph state object. Result=%08x"), HResult);

	TRefCountPtr<ID3D12StateObjectProperties1> PipelineProperties;
	HResult = StateObject->QueryInterface(IID_PPV_ARGS(PipelineProperties.GetInitReference()));
	checkf(SUCCEEDED(HResult), TEXT("Failed to query pipeline properties from the work graph pipeline state object. Result=%08x"), HResult);

	ProgramIdentifier = PipelineProperties->GetProgramIdentifier(ProgramName);

	TRefCountPtr<ID3D12WorkGraphProperties> WorkGraphProperties;
	HResult = StateObject->QueryInterface(IID_PPV_ARGS(WorkGraphProperties.GetInitReference()));
	checkf(SUCCEEDED(HResult), TEXT("Failed to query work graph properties from the work graph pipeline state object. Result=%08x"), HResult);

	UINT WorkGraphIndex = WorkGraphProperties->GetWorkGraphIndex(ProgramName);
	D3D12_WORK_GRAPH_MEMORY_REQUIREMENTS MemoryRequirements = {};
	WorkGraphProperties->GetWorkGraphMemoryRequirements(WorkGraphIndex, &MemoryRequirements);

	MaxRootArgOffset = 0;
	RootArgOffsets.Reset();
	if (bIsShaderBundle)
	{
		RootArgOffsets.AddDefaulted(ShaderBundleNum);
		for (int32 Index = 0; Index < ShaderBundleNum; ++Index)
		{
			if (LocalNodeShaders[Index])
			{
				uint32 NodeIndex = WorkGraphProperties->GetNodeIndex(WorkGraphIndex, D3D12_NODE_ID{ *Initializer.GetShaderBundleNodeName(), (uint32)Index });
				RootArgOffsets[Index] = WorkGraphProperties->GetNodeLocalRootArgumentsTableIndex(WorkGraphIndex, NodeIndex);
				MaxRootArgOffset = FMath::Max(MaxRootArgOffset, RootArgOffsets[Index]);
			}
		}
	}

	ID3D12Resource* BackingMemoryBufferResource = nullptr;
	{
		CD3DX12_RESOURCE_DESC BufferDesc = CD3DX12_RESOURCE_DESC::Buffer(MemoryRequirements.MaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, 65536ull);
		CD3DX12_HEAP_PROPERTIES HeapProperties(D3D12_HEAP_TYPE_DEFAULT);

		HResult = Device->GetDevice()->CreateCommittedResource(	//TODO: don't use raw device?
			&HeapProperties,
			D3D12_HEAP_FLAG_NONE,
			&BufferDesc,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			NULL,
			__uuidof(ID3D12Resource),
			(void**)&BackingMemoryBufferResource);
		checkf(SUCCEEDED(HResult), TEXT("Failed to allocate backing memory for work graph. Result=%08x"), HResult);
	}
	BackingMemoryAddressRange.StartAddress = BackingMemoryBufferResource->GetGPUVirtualAddress();
	BackingMemoryAddressRange.SizeInBytes = MemoryRequirements.MaxSizeInBytes;

#endif // D3D12_RHI_WORKGRAPHS
}

FWorkGraphPipelineStateRHIRef FD3D12DynamicRHI::RHICreateWorkGraphPipelineState(const FWorkGraphPipelineStateInitializer& Initializer)
{
	FD3D12Device* Device = GetAdapter().GetDevice(0); // All pipelines are created on the first node, as they may be used on any other linked GPU.
	return new FD3D12WorkGraphPipelineState(Device, Initializer);
}

#if D3D12_RHI_WORKGRAPHS

/** Struct to collect transitions for all shader bundle dispatches. */
struct FShaderBundleBinderOps
{
	Experimental::TSherwoodSet<FD3D12View*> TransitionViewSet;
	Experimental::TSherwoodSet<FD3D12View*> TransitionClearSet;

	TArray<FD3D12ShaderResourceView*>  TransitionSRVs;
	TArray<FD3D12UnorderedAccessView*> TransitionUAVs;
	TArray<FD3D12UnorderedAccessView*> ClearUAVs;

	inline void AddResourceTransition(FD3D12ShaderResourceView* SRV)
	{
		if (SRV->GetResource()->RequiresResourceStateTracking())
		{
			bool bAlreadyInSet = false;
			TransitionViewSet.Add(SRV, &bAlreadyInSet);
			if (!bAlreadyInSet)
			{
				TransitionSRVs.Add(SRV);
			}
		}
	}

	inline void AddResourceTransition(FD3D12UnorderedAccessView* UAV)
	{
		if (UAV->GetResource()->RequiresResourceStateTracking())
		{
			bool bAlreadyInSet = false;
			TransitionViewSet.Add(UAV, &bAlreadyInSet);
			if (!bAlreadyInSet)
			{
				TransitionUAVs.Add(UAV);
			}
		}
	}

	inline void AddResourceClear(FD3D12UnorderedAccessView* UAV)
	{
		bool bAlreadyInSet = false;
		TransitionClearSet.Add(UAV, &bAlreadyInSet);
		if (!bAlreadyInSet)
		{
			ClearUAVs.Add(UAV);
		}
	}
};

/** Struct to collect shader bundle bindings. */
struct FWorkGraphShaderBundleBinder
{
	FD3D12CommandContext& Context;
	FShaderBundleBinderOps& BinderOps;
	const uint32 GPUIndex;
#if PLATFORM_SUPPORTS_BINDLESS_RENDERING
	const bool bBindlessResources;
	const bool bBindlessSamplers;
#endif

	uint32 CBVVersions[MAX_CBS];
	uint32 SRVVersions[MAX_SRVS];
	uint32 UAVVersions[MAX_UAVS];
	uint32 SamplerVersions[MAX_SAMPLERS];

	uint64 BoundCBVMask = 0;
	uint64 BoundSRVMask = 0;
	uint64 BoundUAVMask = 0;
	uint64 BoundSamplerMask = 0;

	D3D12_CPU_DESCRIPTOR_HANDLE LocalCBVs[MAX_CBS];
	D3D12_CPU_DESCRIPTOR_HANDLE LocalSRVs[MAX_SRVS];
	D3D12_CPU_DESCRIPTOR_HANDLE LocalUAVs[MAX_UAVS];
	D3D12_CPU_DESCRIPTOR_HANDLE LocalSamplers[MAX_SAMPLERS];

	FWorkGraphShaderBundleBinder(FD3D12CommandContext& InContext, FShaderBundleBinderOps& InBinderOps, FD3D12ShaderData const* ShaderData)
		: Context(InContext)
		, BinderOps(InBinderOps)
		, GPUIndex(InContext.GetGPUIndex())
#if PLATFORM_SUPPORTS_BINDLESS_RENDERING
		, bBindlessResources(EnumHasAnyFlags(ShaderData->ResourceCounts.UsageFlags, EShaderResourceUsageFlags::BindlessResources))
		, bBindlessSamplers(EnumHasAnyFlags(ShaderData->ResourceCounts.UsageFlags, EShaderResourceUsageFlags::BindlessSamplers))
#endif
	{
	}

#if PLATFORM_SUPPORTS_BINDLESS_RENDERING
	void SetBindlessHandle(const FRHIDescriptorHandle& Handle, uint32 Offset)
	{
		if (Handle.IsValid())
		{
			checkNoEntry();
			//const uint32 BindlessIndex = Handle.GetIndex();
			//ConstantBuffer.UpdateConstant(reinterpret_cast<const uint8*>(&BindlessIndex), Offset, 4);
		}
	}
#endif

	void SetUAV(FRHIUnorderedAccessView* InUnorderedAccessView, uint32 Index, bool bClearResources = false)
	{
		FD3D12UnorderedAccessView* UAV = FD3D12CommandContext::RetrieveObject<FD3D12UnorderedAccessView_RHI>(InUnorderedAccessView, GPUIndex);
		check(UAV != nullptr);

		if (bClearResources)
		{
			BinderOps.AddResourceClear(UAV);
		}

#if PLATFORM_SUPPORTS_BINDLESS_RENDERING
		if (bBindlessResources)
		{
			checkNoEntry();
			//Context.StateCache.QueueBindlessUAV(Frequency, D3D12UnorderedAccessView);
		}
		else
#endif
		{
			FD3D12OfflineDescriptor Descriptor = UAV->GetOfflineCpuHandle();
			LocalUAVs[Index] = Descriptor;
			UAVVersions[Index] = Descriptor.GetVersion();
			BoundUAVMask |= 1ull << Index;
			BinderOps.AddResourceTransition(UAV);
		}
	}

	void SetSRV(FRHIShaderResourceView* InShaderResourceView, uint32 Index)
	{
		FD3D12ShaderResourceView_RHI* SRV = FD3D12CommandContext::RetrieveObject<FD3D12ShaderResourceView_RHI>(InShaderResourceView, GPUIndex);
		check(SRV != nullptr);

#if PLATFORM_SUPPORTS_BINDLESS_RENDERING
		if (bBindlessResources)
		{
			checkNoEntry();
			//Context.StateCache.QueueBindlessSRV(Frequency, SRV);
		}
		else
#endif
		{
			FD3D12OfflineDescriptor Descriptor = SRV->GetOfflineCpuHandle();
			LocalSRVs[Index] = Descriptor;
			SRVVersions[Index] = Descriptor.GetVersion();
			BoundSRVMask |= 1ull << Index;
			BinderOps.AddResourceTransition(SRV);
		}
	}

	void SetTexture(FRHITexture* InTexture, uint32 Index)
	{
		FD3D12ShaderResourceView* SRV = FD3D12CommandContext::RetrieveTexture(InTexture, GPUIndex)->GetShaderResourceView();
		check(SRV != nullptr);

#if PLATFORM_SUPPORTS_BINDLESS_RENDERING
		if (bBindlessResources)
		{
			checkNoEntry();
			//Context.StateCache.QueueBindlessSRV(Frequency, D3D12ShaderResourceView);
		}
		else
#endif
		{
			FD3D12OfflineDescriptor Descriptor = SRV->GetOfflineCpuHandle();
			LocalSRVs[Index] = Descriptor;
			SRVVersions[Index] = Descriptor.GetVersion();
			BoundSRVMask |= 1ull << Index;
			BinderOps.AddResourceTransition(SRV);
		}
	}

	void SetSampler(FRHISamplerState* InSampler, uint32 Index)
	{
		FD3D12SamplerState* Sampler = FD3D12CommandContext::RetrieveObject<FD3D12SamplerState>(InSampler, GPUIndex);
		check(Sampler != nullptr);

#if PLATFORM_SUPPORTS_BINDLESS_RENDERING
		if (bBindlessSamplers)
		{
			// Nothing to do, only needs constants set
		}
		else
#endif
		{
			FD3D12OfflineDescriptor Descriptor = Sampler->OfflineDescriptor;
			LocalSamplers[Index] = Descriptor;
			SamplerVersions[Index] = Descriptor.GetVersion();
			BoundSamplerMask |= 1ull << Index;
		}
	}

	void SetResourceCollection(FRHIResourceCollection* ResourceCollection, uint32 Index)
	{
#if PLATFORM_SUPPORTS_BINDLESS_RENDERING
		FD3D12ResourceCollection* D3D12ResourceCollection = FD3D12CommandContext::RetrieveObject<FD3D12ResourceCollection>(ResourceCollection, GPUIndex);
		FD3D12ShaderResourceView* SRV = D3D12ResourceCollection ? D3D12ResourceCollection->GetShaderResourceView() : nullptr;

		if (bBindlessResources)
		{
			checkNoEntry();
			//Context.StateCache.QueueBindlessSRV(Frequency, SRV);
		}
#endif // PLATFORM_SUPPORTS_BINDLESS_RENDERING
	}
};

// Record bindings from shader bundle parameters.
static void RecordBindings(
	FD3D12CommandContext* Context,
	FD3D12ExplicitDescriptorCache& TransientDescriptorCache,
	FShaderBundleBinderOps& BinderOps,
	uint32 WorkerIndex,
	FRHIWorkGraphShader* WorkGraphShaderRHI,
	FRHIBatchedShaderParameters const& Parameters,
	FUint32Vector4 const& Constants,
	TArrayView<uint32> RootArgs
)
{
	FD3D12WorkGraphShader* D3D12WorkGraphShader = static_cast<FD3D12WorkGraphShader*>(WorkGraphShaderRHI);
	const uint32 NumSMPs = D3D12WorkGraphShader->ResourceCounts.NumSamplers;
	const uint32 NumSRVs = D3D12WorkGraphShader->ResourceCounts.NumSRVs;
	const uint32 NumCBVs = D3D12WorkGraphShader->ResourceCounts.NumCBs;
	const uint32 NumUAVs = D3D12WorkGraphShader->ResourceCounts.NumUAVs;

	// With shader root constants, we should never hit this expensive path!
	// If we hit this, check if the shaders in the bundle had loose
	// uniform parameters added to it recently, falling into this path.
	check(!D3D12WorkGraphShader->UsesGlobalUniformBuffer());

	FWorkGraphShaderBundleBinder BundleBinder(*Context, BinderOps, D3D12WorkGraphShader);

	FD3D12UniformBuffer* BundleUniformBuffers[MAX_CBS] = { nullptr };

	uint32 UniformBufferMask = 0u;

	const bool bClearUAVResources = false;

#if PLATFORM_SUPPORTS_BINDLESS_RENDERING
	for (const FRHIShaderParameterResource& Parameter : Parameters.BindlessParameters)
	{
		if (FRHIResource* Resource = Parameter.Resource)
		{
			FRHIDescriptorHandle Handle;

			switch (Parameter.Type)
			{
			case FRHIShaderParameterResource::EType::Texture:
				Handle = static_cast<FRHITexture*>(Resource)->GetDefaultBindlessHandle();
				BundleBinder.SetTexture(static_cast<FRHITexture*>(Resource), Parameter.Index);
				break;
			case FRHIShaderParameterResource::EType::ResourceView:
				Handle = static_cast<FRHIShaderResourceView*>(Resource)->GetBindlessHandle();
				BundleBinder.SetSRV(static_cast<FRHIShaderResourceView*>(Resource), Parameter.Index);
				break;
			case FRHIShaderParameterResource::EType::UnorderedAccessView:
				Handle = static_cast<FRHIUnorderedAccessView*>(Resource)->GetBindlessHandle();
				BundleBinder.SetUAV(static_cast<FRHIUnorderedAccessView*>(Resource), Parameter.Index, bClearUAVResources);
				break;
			case FRHIShaderParameterResource::EType::Sampler:
				Handle = static_cast<FRHISamplerState*>(Resource)->GetBindlessHandle();
				BundleBinder.SetSampler(static_cast<FRHISamplerState*>(Resource), Parameter.Index);
				break;
			}

			BundleBinder.SetBindlessHandle(Handle, Parameter.Index);
		}
	}
#endif

	for (const FRHIShaderParameterResource& Parameter : Parameters.ResourceParameters)
	{
		switch (Parameter.Type)
		{
		case FRHIShaderParameterResource::EType::Texture:
			BundleBinder.SetTexture(static_cast<FRHITexture*>(Parameter.Resource), Parameter.Index);
			break;
		case FRHIShaderParameterResource::EType::ResourceView:
			BundleBinder.SetSRV(static_cast<FRHIShaderResourceView*>(Parameter.Resource), Parameter.Index);
			break;
		case FRHIShaderParameterResource::EType::UnorderedAccessView:
			BundleBinder.SetUAV(static_cast<FRHIUnorderedAccessView*>(Parameter.Resource), Parameter.Index, bClearUAVResources);
			break;
		case FRHIShaderParameterResource::EType::Sampler:
			BundleBinder.SetSampler(static_cast<FRHISamplerState*>(Parameter.Resource), Parameter.Index);
			break;
		case FRHIShaderParameterResource::EType::UniformBuffer:
			BundleUniformBuffers[Parameter.Index] = FD3D12CommandContext::RetrieveObject<FD3D12UniformBuffer>(Parameter.Resource, 0 /*GpuIndex*/);
			UniformBufferMask |= (1 << Parameter.Index);
			break;
		case FRHIShaderParameterResource::EType::ResourceCollection:
			BundleBinder.SetResourceCollection(static_cast<FRHIResourceCollection*>(Parameter.Resource), Parameter.Index);
			break;
		default:
			checkf(false, TEXT("Unhandled resource type?"));
			break;
		}
	}

	UE::RHICore::ApplyStaticUniformBuffers(WorkGraphShaderRHI, Context->GetStaticUniformBuffers(),
		[&](int32 BufferIndex, FRHIUniformBuffer* Buffer)
		{
			BundleUniformBuffers[BufferIndex] = Context->RetrieveObject<FD3D12UniformBuffer>(Buffer);
		});


	uint32 FakeDirtyUniformBuffers = ~(0u);
	UE::RHICore::SetResourcesFromTables(BundleBinder, *WorkGraphShaderRHI, FakeDirtyUniformBuffers, BundleUniformBuffers
#if ENABLE_RHI_VALIDATION
		, Context->Tracker
#endif
	);

	for (uint32 CBVIndex = 0; CBVIndex < MAX_CBS; ++CBVIndex)
	{
		FD3D12UniformBuffer* UniformBuffer = BundleUniformBuffers[CBVIndex];
		if (UniformBuffer)
		{
			BundleBinder.BoundCBVMask |= 1ull << CBVIndex;
		}
	}

	// Validate that all resources required by the shader are set
	auto IsCompleteBinding = [](uint32 ExpectedCount, uint64 BoundMask)
	{
		if (ExpectedCount > 64) return false; // Bound resource mask can't be represented by uint64

		// All bits of the mask [0..ExpectedCount) are expected to be set
		uint64 ExpectedMask = ExpectedCount == 64 ? ~0ull : ((1ull << ExpectedCount) - 1);
		return (ExpectedMask & BoundMask) == ExpectedMask;
	};

	check(IsCompleteBinding(D3D12WorkGraphShader->ResourceCounts.NumSRVs, BundleBinder.BoundSRVMask));
	check(IsCompleteBinding(D3D12WorkGraphShader->ResourceCounts.NumUAVs, BundleBinder.BoundUAVMask));
	check(IsCompleteBinding(D3D12WorkGraphShader->ResourceCounts.NumCBs, BundleBinder.BoundCBVMask));
	check(IsCompleteBinding(D3D12WorkGraphShader->ResourceCounts.NumSamplers, BundleBinder.BoundSamplerMask));

	if (NumSRVs > 0)
	{
		const int32 DescriptorTableBaseIndex = TransientDescriptorCache.Allocate(BundleBinder.LocalSRVs, NumSRVs, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, WorkerIndex);
		check(DescriptorTableBaseIndex != INDEX_NONE);

		const uint32 BindSlot = D3D12WorkGraphShader->RootSignature->SRVRDTBindSlot(SF_Compute);
		const uint32 BindSlotOffset = D3D12WorkGraphShader->RootSignature->GetBindSlotOffsetInBytes(BindSlot) / 4;

		const D3D12_GPU_DESCRIPTOR_HANDLE ResourceDescriptorTableBaseGPU = TransientDescriptorCache.ViewHeap.GetDescriptorGPU(DescriptorTableBaseIndex);
		FMemory::Memcpy(&RootArgs[BindSlotOffset], &ResourceDescriptorTableBaseGPU, sizeof(ResourceDescriptorTableBaseGPU));
	}

	if (NumSMPs > 0)
	{
		const int32 DescriptorTableBaseIndex = TransientDescriptorCache.AllocateDeduplicated(BundleBinder.SamplerVersions, BundleBinder.LocalSamplers, NumSMPs, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, WorkerIndex);
		check(DescriptorTableBaseIndex != INDEX_NONE);

		const uint32 BindSlot = D3D12WorkGraphShader->RootSignature->SamplerRDTBindSlot(SF_Compute);
		const uint32 BindSlotOffset = D3D12WorkGraphShader->RootSignature->GetBindSlotOffsetInBytes(BindSlot) / 4;

		const D3D12_GPU_DESCRIPTOR_HANDLE ResourceDescriptorTableBaseGPU = TransientDescriptorCache.SamplerHeap.GetDescriptorGPU(DescriptorTableBaseIndex);
		FMemory::Memcpy(&RootArgs[BindSlotOffset], &ResourceDescriptorTableBaseGPU, sizeof(ResourceDescriptorTableBaseGPU));
	}

	if (NumUAVs > 0)
	{
		const int32 DescriptorTableBaseIndex = TransientDescriptorCache.AllocateDeduplicated(BundleBinder.UAVVersions, BundleBinder.LocalUAVs, NumUAVs, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, WorkerIndex);
		//const int32 DescriptorTableBaseIndex = TransientDescriptorCache.Allocate(BundleBinder.LocalUAVs, NumUAVs, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, WorkerIndex);
		check(DescriptorTableBaseIndex != INDEX_NONE);

		const uint32 BindSlot = D3D12WorkGraphShader->RootSignature->UAVRDTBindSlot(SF_Compute);
		const uint32 BindSlotOffset = D3D12WorkGraphShader->RootSignature->GetBindSlotOffsetInBytes(BindSlot) / 4;

		const D3D12_GPU_DESCRIPTOR_HANDLE ResourceDescriptorTableBaseGPU = TransientDescriptorCache.ViewHeap.GetDescriptorGPU(DescriptorTableBaseIndex);
		FMemory::Memcpy(&RootArgs[BindSlotOffset], &ResourceDescriptorTableBaseGPU, sizeof(ResourceDescriptorTableBaseGPU));
	}

	for (uint32 CBVIndex = 0; CBVIndex < NumCBVs; ++CBVIndex)
	{
		const uint32 BindSlot = D3D12WorkGraphShader->RootSignature->CBVRDBindSlot(SF_Compute, CBVIndex);
		const uint32 BindSlotOffset = D3D12WorkGraphShader->RootSignature->GetBindSlotOffsetInBytes(BindSlot) / 4;

		FD3D12UniformBuffer* UniformBuffer = BundleUniformBuffers[CBVIndex];
		if (UniformBuffer)
		{
			D3D12_GPU_VIRTUAL_ADDRESS Address = UniformBuffer->ResourceLocation.GetGPUVirtualAddress();
			FMemory::Memcpy(&RootArgs[BindSlotOffset], &Address, sizeof(Address));
		}
	}

	const int8 BindSlot = D3D12WorkGraphShader->RootSignature->GetRootConstantsSlot();
	if (BindSlot != -1)
	{
		const uint32 BindSlotOffset = D3D12WorkGraphShader->RootSignature->GetBindSlotOffsetInBytes(BindSlot) / 4;

		RootArgs[BindSlotOffset] = Constants.X;
		RootArgs[BindSlotOffset + 1] = Constants.Y;
		RootArgs[BindSlotOffset + 2] = Constants.Z;
		RootArgs[BindSlotOffset + 3] = Constants.W;
	}
}

#endif // D3D12_RHI_WORKGRAPHS

void FD3D12CommandContext::DispatchWorkGraphShaderBundle(FRHIShaderBundle* ShaderBundle, FRHIBuffer* RecordArgBuffer, TConstArrayView<FRHIShaderParameterResource> SharedBindlessParameters, TConstArrayView<FRHIShaderBundleComputeDispatch> Dispatches)
{
#if D3D12_RHI_WORKGRAPHS

	TRHICommandList_RecursiveHazardous<FD3D12CommandContext> RHICmdList(this);

	FD3D12ShaderBundle* D3D12ShaderBundle = static_cast<FD3D12ShaderBundle*>(FD3D12DynamicRHI::ResourceCast(ShaderBundle));

	TShaderRef<FDispatchShaderBundleWorkGraph> WorkGraphGlobalShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FDispatchShaderBundleWorkGraph>();
	FD3D12WorkGraphShader* WorkGraphGlobalShaderRHI = static_cast<FD3D12WorkGraphShader*>(WorkGraphGlobalShader.GetWorkGraphShader());

	uint32 ViewDescriptorCount = WorkGraphGlobalShaderRHI->ResourceCounts.NumSRVs + WorkGraphGlobalShaderRHI->ResourceCounts.NumCBs + WorkGraphGlobalShaderRHI->ResourceCounts.NumUAVs;
	uint32 SamplerDescriptorCount = WorkGraphGlobalShaderRHI->ResourceCounts.NumSamplers;

	const int32 NumRecords = Dispatches.Num();
	checkf(NumRecords <= FDispatchShaderBundleWorkGraph::GetMaxShaderBundleSize(), TEXT("Too many entries in a shader bundle (%d). Try increasing 'r.ShaderBundle.MaxSize'"), NumRecords);

	TArray<uint32> ValidRecords;
	ValidRecords.Reserve(NumRecords);
	TArray<FRHIWorkGraphShader*> LocalNodeShaders;
	LocalNodeShaders.Reserve(NumRecords);

	for (int32 DispatchIndex = 0; DispatchIndex < NumRecords; ++DispatchIndex)
	{
		const FRHIShaderBundleComputeDispatch& Dispatch = Dispatches[DispatchIndex];
		FRHIWorkGraphShader* Shader = Dispatch.IsValid() ? Dispatch.WorkGraphShader : nullptr;
		LocalNodeShaders.Add(Shader);
		if (Shader != nullptr)
		{
			ValidRecords.Emplace(uint32(DispatchIndex));

			if (FD3D12WorkGraphShader* D3D12Shader = FD3D12DynamicRHI::ResourceCast(Shader))
			{
				ViewDescriptorCount += D3D12Shader->ResourceCounts.NumSRVs + D3D12Shader->ResourceCounts.NumCBs + D3D12Shader->ResourceCounts.NumUAVs;
				SamplerDescriptorCount += D3D12Shader->ResourceCounts.NumSamplers;
			}
		}
	}
	const int32 NumValidRecords = ValidRecords.Num();

	FWorkGraphPipelineStateInitializer Initializer;
	Initializer.SetProgramName(TEXT("ShaderBundleWorkGraph"));
	Initializer.SetShader(WorkGraphGlobalShaderRHI);
	Initializer.SetShaderBundleNodeTable(LocalNodeShaders, TEXT("ShaderBundleNode"));

	FWorkGraphPipelineState* WorkGraphPipelineState = PipelineStateCache::GetAndOrCreateWorkGraphPipelineState(RHICmdList, Initializer);
	FD3D12WorkGraphPipelineState* Pipeline = static_cast<FD3D12WorkGraphPipelineState*>(GetRHIWorkGraphPipelineState(WorkGraphPipelineState));

	const uint32 NumViewDescriptors = ViewDescriptorCount;
	const uint32 NumSamplerDescriptors = SamplerDescriptorCount;

	const uint32 MaxWorkers = 4u;
	const uint32 NumWorkerThreads = FTaskGraphInterface::Get().GetNumWorkerThreads();
	const uint32 MaxTasks = FApp::ShouldUseThreadingForPerformance() ? FMath::Min<uint32>(NumWorkerThreads, MaxWorkers) : 1u;

	struct FTaskContext
	{
		uint32 WorkerIndex = 0;
	};

	TArray<FTaskContext, TInlineAllocator<MaxWorkers>> TaskContexts;
	for (uint32 WorkerIndex = 0; WorkerIndex < MaxTasks; ++WorkerIndex)
	{
		TaskContexts.Add(FTaskContext{ WorkerIndex });
	}

	FD3D12ExplicitDescriptorCache TransientDescriptorCache(GetParentDevice(), MaxTasks /* Worker Count */);
	TransientDescriptorCache.Init(0, NumViewDescriptors, NumSamplerDescriptors, ERHIBindlessConfiguration::AllShaders);

	TArray<FShaderBundleBinderOps, TInlineAllocator<MaxWorkers>> BinderOps;
	BinderOps.SetNum(MaxTasks);

	TResourceArray<uint32> LocalRootArgs;
	uint32 MinRootArgBufferSizeInDWords = (Pipeline->RootArgStrideInBytes / 4) * (Pipeline->MaxRootArgOffset + 1);
	LocalRootArgs.AddZeroed(MinRootArgBufferSizeInDWords);

	auto RecordTask = [this, &LocalRootArgs, Pipeline, &TransientDescriptorCache, &ValidRecords, &Dispatches, &BinderOps](FTaskContext& Context, int32 RecordIndex)
	{
		uint32 DispatchIndex = ValidRecords[RecordIndex];
		const FRHIShaderBundleComputeDispatch& Dispatch = Dispatches[DispatchIndex];

		check(Pipeline->RootArgOffsets.IsValidIndex(DispatchIndex));
		uint32 RootArgOffset = Pipeline->RootArgOffsets[DispatchIndex];
		check((Pipeline->RootArgStrideInBytes / 4) * (RootArgOffset + 1) <= (uint32)LocalRootArgs.Num());

		RecordBindings(
			this,
			TransientDescriptorCache,
			BinderOps[Context.WorkerIndex],
			Context.WorkerIndex,
			Dispatch.WorkGraphShader,
			*Dispatch.Parameters,
			Dispatch.Constants,
			MakeArrayView(&LocalRootArgs[RootArgOffset * Pipeline->RootArgStrideInBytes / 4], Pipeline->RootArgStrideInBytes / 4)
		);
	};

	// One helper worker task will be created at most per this many work items, plus one worker for current thread (unless running on a task thread),
	// up to a hard maximum of MaxWorkers.
	// Internally, parallel for tasks still subdivide the work into smaller chunks and perform fine-grained load-balancing.
	const int32 ItemsPerTask = 1024;

	ParallelForWithExistingTaskContext(TEXT("DispatchShaderBundle"), MakeArrayView(TaskContexts), ValidRecords.Num(), ItemsPerTask, RecordTask);

	// Upload local root arguments table.
	D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE NodeLocalRootArgumentsTable{ 0, 0, 0 };
	if (ValidRecords.Num() && LocalRootArgs.Num())
	{
		// todo: Check if copy queue is the optimal way to upload the root args.
		// todo: Use a single buffer owned by the shader bundle RHI object (needs a copy operation that doesn't complain about multiple uploads).
		D3D12_RESOURCE_DESC Desc = CD3DX12_RESOURCE_DESC::Buffer(LocalRootArgs.GetResourceDataSize(), D3D12_RESOURCE_FLAG_NONE);
		FD3D12Buffer* RootArgBuffer = GetParentDevice()->GetParentAdapter()->CreateRHIBuffer(
			Desc, 16, FRHIBufferDesc(Desc.Width, 0, BUF_Static), ED3D12ResourceStateMode::MultiState,
			D3D12_RESOURCE_STATE_COPY_DEST, true, FRHIGPUMask::FromIndex(GetParentDevice()->GetGPUIndex()), nullptr, TEXT("BundleRecordBuffer"));

		BatchedSyncPoints.ToWait.Emplace(RootArgBuffer->UploadResourceDataViaCopyQueue(&LocalRootArgs));
		TransitionResource(RootArgBuffer->GetResource(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON, 0);

		NodeLocalRootArgumentsTable.StartAddress = RootArgBuffer->ResourceLocation.GetGPUVirtualAddress();
		NodeLocalRootArgumentsTable.SizeInBytes = RootArgBuffer->ResourceLocation.GetSize();
		NodeLocalRootArgumentsTable.StrideInBytes = Pipeline->RootArgStrideInBytes;
	}

	// Apply Binder Ops
	{
		for (int32 WorkerIndex = 1; WorkerIndex < BinderOps.Num(); ++WorkerIndex)
		{
			for (FD3D12ShaderResourceView* SRV : BinderOps[WorkerIndex].TransitionSRVs)
			{
				BinderOps[0].AddResourceTransition(SRV);
			}

			for (FD3D12UnorderedAccessView* UAV : BinderOps[WorkerIndex].TransitionUAVs)
			{
				BinderOps[0].AddResourceTransition(UAV);
			}

			BinderOps[WorkerIndex].TransitionSRVs.Empty();
			BinderOps[WorkerIndex].TransitionUAVs.Empty();
			BinderOps[WorkerIndex].TransitionViewSet.Empty();

			BinderOps[WorkerIndex].ClearUAVs.Empty();
			BinderOps[WorkerIndex].TransitionClearSet.Empty();
		}

		for (FD3D12UnorderedAccessView* UAV : BinderOps[0].ClearUAVs)
		{
			ClearShaderResources(UAV, EShaderParameterTypeMask::SRVMask);
		}

		for (FD3D12ShaderResourceView* SRV : BinderOps[0].TransitionSRVs)
		{
			TransitionResource(SRV, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		}

		for (FD3D12UnorderedAccessView* UAV : BinderOps[0].TransitionUAVs)
		{
			TransitionResource(UAV, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		}
	}

	FlushResourceBarriers();

	// Apply the transient descriptor heaps.
	SetExplicitDescriptorCache(TransientDescriptorCache);

	TSharedPtr<FD3D12ShaderResourceView> RecordArgBufferSRV;

	// Gather root arguments for global work graph.
	int32 DispatchSRVTable = INDEX_NONE;
	{
		D3D12_CPU_DESCRIPTOR_HANDLE LocalSRVs[MAX_SRVS];

		FD3D12Buffer* RecordArgBufferPtr = FD3D12DynamicRHI::ResourceCast(RecordArgBuffer);

		D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
		SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		SRVDesc.Format = DXGI_FORMAT_R32_TYPELESS;
		SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		SRVDesc.Buffer.FirstElement = 0;
		SRVDesc.Buffer.NumElements = RecordArgBufferPtr->GetSize() >> 2u;
		SRVDesc.Buffer.StructureByteStride = 0;
		SRVDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;

		RecordArgBufferSRV = MakeShared<FD3D12ShaderResourceView>(GetParentDevice(), nullptr);			// Always single GPU object, so FirstLinkedObject is nullptr
		RecordArgBufferSRV->CreateView(RecordArgBufferPtr, SRVDesc, FD3D12ShaderResourceView::EFlags::None);

		LocalSRVs[WorkGraphGlobalShader->RecordArgBufferParam.GetBaseIndex()] = RecordArgBufferSRV->GetOfflineCpuHandle();
		DispatchSRVTable = TransientDescriptorCache.Allocate(LocalSRVs, 1, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 0);
	}
	check(DispatchSRVTable != INDEX_NONE);

	const D3D12_GPU_DESCRIPTOR_HANDLE DispatchSRVHandle = TransientDescriptorCache.ViewHeap.GetDescriptorGPU(DispatchSRVTable);
	const uint32 SRVBindSlot = WorkGraphGlobalShaderRHI->RootSignature->SRVRDTBindSlot(SF_Compute);
	check(SRVBindSlot != 0xFF);

	// Kick off the work graph	
	GraphicsCommandList()->SetComputeRootSignature(WorkGraphGlobalShaderRHI->RootSignature->GetRootSignature());
	GraphicsCommandList()->SetComputeRootDescriptorTable(SRVBindSlot, DispatchSRVHandle);

	D3D12_SET_PROGRAM_DESC SetProgramDesc = {};
	SetProgramDesc.Type = D3D12_PROGRAM_TYPE_WORK_GRAPH;
	SetProgramDesc.WorkGraph.ProgramIdentifier = Pipeline->ProgramIdentifier;
	SetProgramDesc.WorkGraph.Flags = !Pipeline->bInitialized ? D3D12_SET_WORK_GRAPH_FLAG_INITIALIZE : D3D12_SET_WORK_GRAPH_FLAG_NONE;
	SetProgramDesc.WorkGraph.BackingMemory = Pipeline->BackingMemoryAddressRange;
	SetProgramDesc.WorkGraph.NodeLocalRootArgumentsTable = NodeLocalRootArgumentsTable;
	GraphicsCommandList10()->SetProgram(&SetProgramDesc);

	FDispatchShaderBundleWorkGraph::FEntryNodeRecord InputRecord = FDispatchShaderBundleWorkGraph::MakeInputRecord(NumRecords, ShaderBundle->ArgOffset, ShaderBundle->ArgStride);

	if (!GShaderBundleSkipDispatch)
	{
		D3D12_DISPATCH_GRAPH_DESC DispatchGraphDesc = {};
		DispatchGraphDesc.Mode = D3D12_DISPATCH_MODE_NODE_CPU_INPUT;
		DispatchGraphDesc.NodeCPUInput.EntrypointIndex = 0;
		DispatchGraphDesc.NodeCPUInput.NumRecords = 1;
		DispatchGraphDesc.NodeCPUInput.RecordStrideInBytes = sizeof(InputRecord);
		DispatchGraphDesc.NodeCPUInput.pRecords = &InputRecord;
		GraphicsCommandList10()->DispatchGraph(&DispatchGraphDesc);
	}

	// Pipeline state memory should now be initialized.
	Pipeline->bInitialized = true;

	// Restore old global descriptor heaps
	UnsetExplicitDescriptorCache();

	// We did not write through the state cache, so we need to invalidate it so subsequent workloads correctly re-bind state
	StateCache.DirtyState();

	ConditionalSplitCommandList();

#endif // D3D12_RHI_WORKGRAPHS
}
