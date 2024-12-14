// Copyright Epic Games, Inc. All Rights Reserved.

#include "RayTracingScene.h"

#if RHI_RAYTRACING

#include "RayTracingInstanceBufferUtil.h"
#include "RenderCore.h"
#include "RayTracingDefinitions.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RaytracingOptions.h"
#include "PrimitiveSceneProxy.h"
#include "SceneUniformBuffer.h"
#include "SceneRendering.h"
#include "RayTracingInstanceCulling.h"

static TAutoConsoleVariable<int32> CVarRayTracingSceneBuildMode(
	TEXT("r.RayTracing.Scene.BuildMode"),
	1,
	TEXT("Controls the mode in which ray tracing scene is built:\n")
	TEXT(" 0: Fast build\n")
	TEXT(" 1: Fast trace (default)\n"),
	ECVF_RenderThreadSafe | ECVF_Scalability
);

BEGIN_SHADER_PARAMETER_STRUCT(FBuildInstanceBufferPassParams, )
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer, InstanceBuffer)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer, OutputStats)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer, DebugInstanceGPUSceneIndexBuffer)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneUniformParameters, Scene)
END_SHADER_PARAMETER_STRUCT()

const FRayTracingScene::FInstanceHandle FRayTracingScene::INVALID_INSTANCE_HANDLE = FInstanceHandle();

// Round up buffer sizes to some multiple to avoid pathological growth reallocations.
static constexpr uint32 AllocationGranularity = 8 * 1024;
static constexpr uint64 BufferAllocationGranularity = 16 * 1024 * 1024;

FRayTracingScene::FRayTracingScene()
{
	const uint8 NumLayers = uint8(ERayTracingSceneLayer::NUM);
	Layers.AddDefaulted(NumLayers);
}

FRayTracingScene::~FRayTracingScene()
{
	ReleaseReadbackBuffers();
}

void FRayTracingScene::BuildInitializationData()
{
	const uint8 NumLayers = uint8(ERayTracingSceneLayer::NUM);

	for (uint32 LayerIndex = 0; LayerIndex < NumLayers; ++LayerIndex)
	{
		FLayer& Layer = Layers[LayerIndex];

		Layer.InitializationData = BuildRayTracingSceneInitializationData(Layer.Instances);
	}

	bInitializationDataBuilt = true;
}

void FRayTracingScene::InitPreViewTranslation(const FViewMatrices& ViewMatrices)
{
	PreViewTranslation = ViewMatrices.GetPreViewTranslation();
}

void FRayTracingScene::Create(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FGPUScene* GPUScene, ERDGPassFlags ComputePassFlags)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FRayTracingScene::Create);
	QUICK_SCOPE_CYCLE_COUNTER(STAT_RayTracingScene_Create);

	const ERayTracingAccelerationStructureFlags BuildFlags = CVarRayTracingSceneBuildMode.GetValueOnRenderThread()
		? ERayTracingAccelerationStructureFlags::FastTrace
		: ERayTracingAccelerationStructureFlags::FastBuild;

	if (!bInitializationDataBuilt)
	{
		BuildInitializationData();
	}

	bUsedThisFrame = true;

	FRHICommandListBase& RHICmdList = GraphBuilder.RHICmdList;

	const uint8 NumLayers = uint8(ERayTracingSceneLayer::NUM);

	for (uint32 LayerIndex = 0; LayerIndex < NumLayers; ++LayerIndex)
	{
		FLayer& Layer = Layers[LayerIndex];

		{
			TArray<TRefCountPtr<FRHIRayTracingGeometry>> ReferencedGeometries;
			TArray<FRHIRayTracingGeometry*> PerInstanceGeometries;

			FRayTracingSceneInitializer Initializer;
			Initializer.DebugName = FName(TEXT("FRayTracingScene"));
			Initializer.MaxNumInstances = Layer.InitializationData.NumNativeGPUSceneInstances + Layer.InitializationData.NumNativeCPUInstances;
			Initializer.NumTotalSegments = Layer.InitializationData.TotalNumSegments;
			Initializer.BuildFlags = BuildFlags;

			Layer.RayTracingSceneRHI = RHICreateRayTracingScene(MoveTemp(Initializer));
		}

		const uint32 NumNativeInstances = Layer.InitializationData.NumNativeGPUSceneInstances + Layer.InitializationData.NumNativeCPUInstances;
		const uint32 NumNativeInstancesAligned = FMath::DivideAndRoundUp(FMath::Max(NumNativeInstances, 1U), AllocationGranularity) * AllocationGranularity;
		const uint32 NumTransformsAligned = FMath::DivideAndRoundUp(FMath::Max(Layer.InitializationData.NumNativeCPUInstances, 1U), AllocationGranularity) * AllocationGranularity;

		FRayTracingAccelerationStructureSize SizeInfo = Layer.RayTracingSceneRHI->GetSizeInfo();
		SizeInfo.ResultSize = FMath::DivideAndRoundUp(FMath::Max(SizeInfo.ResultSize, 1ull), BufferAllocationGranularity) * BufferAllocationGranularity;

		// Allocate GPU buffer if current one is too small or significantly larger than what we need.
		if (!Layer.RayTracingScenePooledBuffer.IsValid()
			|| SizeInfo.ResultSize > Layer.RayTracingScenePooledBuffer->GetSize()
			|| SizeInfo.ResultSize < Layer.RayTracingScenePooledBuffer->GetSize() / 2)
		{
			FRDGBufferDesc Desc = FRDGBufferDesc::CreateBufferDesc(1, uint32(SizeInfo.ResultSize));
			Desc.Usage = EBufferUsageFlags::AccelerationStructure;

			Layer.RayTracingScenePooledBuffer = AllocatePooledBuffer(Desc, TEXT("FRayTracingScene::SceneBuffer"));
		}

		Layer.RayTracingSceneBufferRDG = GraphBuilder.RegisterExternalBuffer(Layer.RayTracingScenePooledBuffer);
		Layer.RayTracingSceneBufferSRV = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(Layer.RayTracingSceneBufferRDG, Layer.RayTracingSceneRHI, 0));

		{
			const uint64 ScratchAlignment = GRHIRayTracingScratchBufferAlignment;
			FRDGBufferDesc ScratchBufferDesc;
			ScratchBufferDesc.Usage = EBufferUsageFlags::RayTracingScratch | EBufferUsageFlags::StructuredBuffer;
			ScratchBufferDesc.BytesPerElement = uint32(ScratchAlignment);
			ScratchBufferDesc.NumElements = uint32(FMath::DivideAndRoundUp(SizeInfo.BuildScratchSize, ScratchAlignment));

			Layer.BuildScratchBuffer = GraphBuilder.CreateBuffer(ScratchBufferDesc, TEXT("FRayTracingScene::ScratchBuffer"));
		}

		{
			FRDGBufferDesc InstanceBufferDesc;
			InstanceBufferDesc.Usage = EBufferUsageFlags::UnorderedAccess | EBufferUsageFlags::ShaderResource | EBufferUsageFlags::StructuredBuffer;
			InstanceBufferDesc.BytesPerElement = GRHIRayTracingInstanceDescriptorSize;
			InstanceBufferDesc.NumElements = NumNativeInstancesAligned;

			Layer.InstanceBuffer = GraphBuilder.CreateBuffer(InstanceBufferDesc, TEXT("FRayTracingScene::InstanceBuffer"));
		}

		{
			// Round to PoT to avoid resizing too often
			const uint32 NumGeometries = FMath::RoundUpToPowerOfTwo(Layer.InitializationData.ReferencedGeometries.Num());
			const uint32 AccelerationStructureAddressesBufferSize = NumGeometries * sizeof(FRayTracingAccelerationStructureAddress);

			if (Layer.AccelerationStructureAddressesBuffer.NumBytes < AccelerationStructureAddressesBufferSize)
			{
				// Need to pass "BUF_MultiGPUAllocate", as virtual addresses are different per GPU
				Layer.AccelerationStructureAddressesBuffer.Initialize(
					GraphBuilder.RHICmdList, TEXT("FRayTracingScene::AccelerationStructureAddressesBuffer"), AccelerationStructureAddressesBufferSize, BUF_Volatile | BUF_MultiGPUAllocate);
			}
		}

		{
			// Create/resize instance upload buffer (if necessary)
			const uint32 UploadBufferSize = NumNativeInstancesAligned * sizeof(FRayTracingInstanceDescriptorInput);

			if (!Layer.InstanceUploadBuffer.IsValid()
				|| UploadBufferSize > Layer.InstanceUploadBuffer->GetSize()
				|| UploadBufferSize < Layer.InstanceUploadBuffer->GetSize() / 2)
			{
				FRHIResourceCreateInfo CreateInfo(TEXT("FRayTracingScene::InstanceUploadBuffer"));
				Layer.InstanceUploadBuffer = RHICmdList.CreateStructuredBuffer(sizeof(FRayTracingInstanceDescriptorInput), UploadBufferSize, BUF_ShaderResource | BUF_Volatile, CreateInfo);
				Layer.InstanceUploadSRV = RHICmdList.CreateShaderResourceView(Layer.InstanceUploadBuffer);
			}
		}

		{
			const uint32 UploadBufferSize = NumTransformsAligned * sizeof(FVector4f) * 3;

			// Create/resize transform upload buffer (if necessary)
			if (!Layer.TransformUploadBuffer.IsValid()
				|| UploadBufferSize > Layer.TransformUploadBuffer->GetSize()
				|| UploadBufferSize < Layer.TransformUploadBuffer->GetSize() / 2)
			{
				FRHIResourceCreateInfo CreateInfo(TEXT("FRayTracingScene::TransformUploadBuffer"));
				Layer.TransformUploadBuffer = RHICmdList.CreateStructuredBuffer(sizeof(FVector4f), UploadBufferSize, BUF_ShaderResource | BUF_Volatile, CreateInfo);
				Layer.TransformUploadSRV = RHICmdList.CreateShaderResourceView(Layer.TransformUploadBuffer);
			}
		}

#if STATS
		FRDGBufferDesc OutputStatsBufferDesc(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 1));
		OutputStatsBufferDesc.Usage |= BUF_SourceCopy;

		FRDGBufferRef OutputStatsBuffer = GraphBuilder.CreateBuffer(OutputStatsBufferDesc, TEXT("FRayTracingScene::OutputStatsBuffer"));
		FRDGBufferUAVRef OutputStatsBufferUAV = GraphBuilder.CreateUAV(OutputStatsBuffer);
		AddClearUAVPass(GraphBuilder, OutputStatsBufferUAV, 0, ComputePassFlags);
#endif

		FRDGBufferUAVRef DebugInstanceGPUSceneIndexBufferUAV = nullptr;
		if (bNeedsDebugInstanceGPUSceneIndexBuffer)
		{
			FRDGBufferDesc DebugInstanceGPUSceneIndexBufferDesc;
			DebugInstanceGPUSceneIndexBufferDesc.Usage = EBufferUsageFlags::UnorderedAccess | EBufferUsageFlags::ShaderResource | EBufferUsageFlags::StructuredBuffer;
			DebugInstanceGPUSceneIndexBufferDesc.BytesPerElement = sizeof(uint32);
			DebugInstanceGPUSceneIndexBufferDesc.NumElements = FMath::Max(NumNativeInstances, 1u);

			Layer.DebugInstanceGPUSceneIndexBuffer = GraphBuilder.CreateBuffer(DebugInstanceGPUSceneIndexBufferDesc, TEXT("FRayTracingScene::DebugInstanceGPUSceneIndexBuffer"));
			DebugInstanceGPUSceneIndexBufferUAV = GraphBuilder.CreateUAV(Layer.DebugInstanceGPUSceneIndexBuffer);

			AddClearUAVPass(GraphBuilder, DebugInstanceGPUSceneIndexBufferUAV, 0xFFFFFFFF);
		}

		if (Layer.InstancesDebugData.Num() > 0 && NumNativeInstances > 0)
		{
			// Create InstanceDebugBuffer (one entry per instance in TLAS)
			// This requires replicating the data in InstancesDebugData (one entry per FRayTracingGeometryInstance) according to NumTransforms in each geometry instance

			check(Layer.InstancesDebugData.Num() == Layer.Instances.Num());

			FRDGUploadData<FRayTracingInstanceDebugData> UploadData(GraphBuilder, NumNativeInstances);

			{
				const uint32 NumItems = Layer.InstancesDebugData.Num();

				// Distribute work evenly to the available task graph workers based on NumItems.
				const uint32 TargetItemsPerTask = 512;
				const uint32 NumThreads = FMath::Min(FTaskGraphInterface::Get().GetNumWorkerThreads(), CVarRHICmdWidth.GetValueOnRenderThread());
				const uint32 NumTasks = FMath::Min(NumThreads, FMath::DivideAndRoundUp(NumItems, TargetItemsPerTask));
				const uint32 NumItemsPerTask = FMath::DivideAndRoundUp(NumItems, NumTasks);

				for (uint32 TaskIndex = 0; TaskIndex < NumTasks; ++TaskIndex)
				{
					const uint32 TaskFirstItemIndex = TaskIndex * NumItemsPerTask;
					const uint32 TaskNumItems = FMath::Min(NumItemsPerTask, NumItems - TaskFirstItemIndex);

					TConstArrayView<FRayTracingGeometryInstance> TaskInstancesData(Layer.Instances.GetData() + TaskFirstItemIndex, TaskNumItems);
					TConstArrayView<FRayTracingInstanceDebugData> TaskInstancesDebugData(Layer.InstancesDebugData.GetData() + TaskFirstItemIndex, TaskNumItems);
					TConstArrayView<uint32> TaskBaseInstancePrefixSum(Layer.InitializationData.BaseInstancePrefixSum.GetData() + TaskFirstItemIndex, TaskNumItems);

					GraphBuilder.AddSetupTask([UploadData, TaskInstancesDebugData, TaskInstancesData, TaskBaseInstancePrefixSum]()
						{
							TRACE_CPUPROFILER_EVENT_SCOPE(FillRayTracingInstanceDebugBuffer);

							for (int32 Index = 0; Index < TaskInstancesDebugData.Num(); ++Index)
							{
								const FRayTracingGeometryInstance& SceneInstance = TaskInstancesData[Index];
								const uint32 BaseInstanceIndex = TaskBaseInstancePrefixSum[Index];

								for (uint32 TransformIndex = 0; TransformIndex < SceneInstance.NumTransforms; ++TransformIndex)
								{
									// write data in the same order used in InstanceBuffer used to build TLAS / InstanceIndex() in hit shaders
									UploadData[BaseInstanceIndex + TransformIndex] = TaskInstancesDebugData[Index];
								}
							}
						});
				}
			}

			Layer.InstanceDebugBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("FRayTracingScene::InstanceDebugData"), UploadData);
		}

		if (NumNativeInstances > 0)
		{
			// Fill instance upload buffer on separate thread since results are only needed in RHI thread
			GraphBuilder.AddCommandListSetupTask(
				[NumNativeInstances,
				NumNativeGPUSceneInstances = Layer.InitializationData.NumNativeGPUSceneInstances,
				NumNativeCPUInstances = Layer.InitializationData.NumNativeCPUInstances,
				Instances = MakeArrayView(Layer.Instances),
				InstanceGeometryIndices = MakeArrayView(Layer.InitializationData.InstanceGeometryIndices),
				BaseUploadBufferOffsets = MakeArrayView(Layer.InitializationData.BaseUploadBufferOffsets),
				BaseInstancePrefixSum = MakeArrayView(Layer.InitializationData.BaseInstancePrefixSum),
				RayTracingSceneRHI = Layer.RayTracingSceneRHI,
				InstanceUploadBuffer = Layer.InstanceUploadBuffer,
				TransformUploadBuffer = Layer.TransformUploadBuffer,
				PreViewTranslation = this->PreViewTranslation](FRHICommandList& RHICmdList)
				{
					FOptionalTaskTagScope TaskTagScope(ETaskTag::EParallelRenderingThread);

					const uint32 InstanceUploadBytes = NumNativeInstances * sizeof(FRayTracingInstanceDescriptorInput);
					const uint32 TransformUploadBytes = NumNativeCPUInstances * 3 * sizeof(FVector4f);

					FRayTracingInstanceDescriptorInput* InstanceUploadData = (FRayTracingInstanceDescriptorInput*)RHICmdList.LockBuffer(InstanceUploadBuffer, 0, InstanceUploadBytes, RLM_WriteOnly);
					FVector4f* TransformUploadData = (NumNativeCPUInstances > 0) ? (FVector4f*)RHICmdList.LockBuffer(TransformUploadBuffer, 0, TransformUploadBytes, RLM_WriteOnly) : nullptr;

					FillRayTracingInstanceUploadBuffer(
						RayTracingSceneRHI,
						PreViewTranslation,
						Instances,
						InstanceGeometryIndices,
						BaseUploadBufferOffsets,
						BaseInstancePrefixSum,
						NumNativeGPUSceneInstances,
						NumNativeCPUInstances,
						MakeArrayView(InstanceUploadData, NumNativeInstances),
						MakeArrayView(TransformUploadData, NumNativeCPUInstances * 3));

					RHICmdList.UnlockBuffer(InstanceUploadBuffer);

					if (NumNativeCPUInstances > 0)
					{
						RHICmdList.UnlockBuffer(TransformUploadBuffer);
					}
				});

			GraphBuilder.AddCommandListSetupTask([&Layer](FRHICommandList& RHICmdList)
				{
					for (uint32 GPUIndex : RHICmdList.GetGPUMask())
					{
						FRayTracingAccelerationStructureAddress* AddressesPtr = (FRayTracingAccelerationStructureAddress*)RHICmdList.LockBufferMGPU(
							Layer.AccelerationStructureAddressesBuffer.Buffer,
							GPUIndex,
							0,
							Layer.InitializationData.ReferencedGeometries.Num() * sizeof(FRayTracingAccelerationStructureAddress), RLM_WriteOnly);

						const TArrayView<FRHIRayTracingGeometry*> ReferencedGeometries = RHICmdList.AllocArray(MakeConstArrayView(Layer.InitializationData.ReferencedGeometries));

						RHICmdList.EnqueueLambda([AddressesPtr, ReferencedGeometries, GPUIndex](FRHICommandListBase&)
							{
								TRACE_CPUPROFILER_EVENT_SCOPE(GetAccelerationStructuresAddresses);

								for (int32 GeometryIndex = 0; GeometryIndex < ReferencedGeometries.Num(); ++GeometryIndex)
								{
									AddressesPtr[GeometryIndex] = ReferencedGeometries[GeometryIndex]->GetAccelerationStructureAddress(GPUIndex);
								}
							});

						RHICmdList.UnlockBufferMGPU(Layer.AccelerationStructureAddressesBuffer.Buffer, GPUIndex);
					}
				});

			FBuildInstanceBufferPassParams* PassParams = GraphBuilder.AllocParameters<FBuildInstanceBufferPassParams>();
			PassParams->InstanceBuffer = GraphBuilder.CreateUAV(Layer.InstanceBuffer);
			PassParams->DebugInstanceGPUSceneIndexBuffer = DebugInstanceGPUSceneIndexBufferUAV;
			PassParams->Scene = View.GetSceneUniforms().GetBuffer(GraphBuilder);

#if STATS
			PassParams->OutputStats = OutputStatsBufferUAV;
#endif

			GraphBuilder.AddPass(
				RDG_EVENT_NAME("RayTracingBuildInstanceBuffer"),
				PassParams,
				ComputePassFlags,
				[PassParams,
				this,
				&Layer,
				GPUScene,
				NumNativeGPUSceneInstances = Layer.InitializationData.NumNativeGPUSceneInstances,
				NumNativeCPUInstances = Layer.InitializationData.NumNativeCPUInstances,
				CullingParameters = View.RayTracingCullingParameters
				](FRHICommandList& RHICmdList)
				{
					BuildRayTracingInstanceBuffer(
						RHICmdList,
						GPUScene,
						FDFVector3(PreViewTranslation),
						PassParams->InstanceBuffer->GetRHI(),
						Layer.InstanceUploadSRV,
						Layer.AccelerationStructureAddressesBuffer.SRV,
						Layer.TransformUploadSRV,
						NumNativeGPUSceneInstances,
						NumNativeCPUInstances,
						CullingParameters.bUseInstanceCulling ? &CullingParameters : nullptr,
						PassParams->OutputStats ? PassParams->OutputStats->GetRHI() : nullptr,
						PassParams->DebugInstanceGPUSceneIndexBuffer ? PassParams->DebugInstanceGPUSceneIndexBuffer->GetRHI() : nullptr);
				});
		}

#if STATS
		// Update stats
		// Currently only supported for Base Layer
		if(LayerIndex == uint8(ERayTracingSceneLayer::Base))
		{
			//  if necessary create readback buffers
			if (StatsReadbackBuffers.IsEmpty())
			{
				StatsReadbackBuffers.SetNum(MaxReadbackBuffers);

				for (uint32 Index = 0; Index < MaxReadbackBuffers; ++Index)
				{
					StatsReadbackBuffers[Index] = new FRHIGPUBufferReadback(TEXT("FRayTracingScene::StatsReadbackBuffer"));
				}
			}

			// copy stats to readback buffer
			{
				AddReadbackBufferPass(GraphBuilder, RDG_EVENT_NAME("FRayTracingScene::StatsReadback"), OutputStatsBuffer,
					[ReadbackBuffer = StatsReadbackBuffers[StatsReadbackBuffersWriteIndex], OutputStatsBuffer](FRDGAsyncTask, FRHICommandList& RHICmdList)
					{
						ReadbackBuffer->EnqueueCopy(RHICmdList, OutputStatsBuffer->GetRHI(), 0u);
					});

				StatsReadbackBuffersWriteIndex = (StatsReadbackBuffersWriteIndex + 1u) % MaxReadbackBuffers;
				StatsReadbackBuffersNumPending = FMath::Min(StatsReadbackBuffersNumPending + 1u, MaxReadbackBuffers);
			}

			// process ready results
			while (StatsReadbackBuffersNumPending > 0)
			{
				uint32 Index = (StatsReadbackBuffersWriteIndex + MaxReadbackBuffers - StatsReadbackBuffersNumPending) % MaxReadbackBuffers;
				FRHIGPUBufferReadback* ReadbackBuffer = StatsReadbackBuffers[Index];
				if (ReadbackBuffer->IsReady())
				{
					StatsReadbackBuffersNumPending--;

					auto ReadbackBufferPtr = (const uint32*)ReadbackBuffer->Lock(sizeof(uint32));

					NumActiveInstances = ReadbackBufferPtr[0];

					ReadbackBuffer->Unlock();
				}
				else
				{
					break;
				}
			}

			SET_DWORD_STAT(STAT_RayTracingTotalInstances, NumNativeInstances);
			SET_DWORD_STAT(STAT_RayTracingActiveInstances, FMath::Min(NumActiveInstances, NumNativeInstances));
		}
#endif
	}

#if DO_CHECK
	uint32 LayersTotalNumSegments = 0;
	for (uint32 LayerIndex = 0; LayerIndex < NumLayers; ++LayerIndex)
	{
		LayersTotalNumSegments += Layers[LayerIndex].InitializationData.TotalNumSegments;
	}
	
	checkf(LayersTotalNumSegments <= NumSegments, TEXT("Ray tracing scene layers use more segments than the number used to create SBTs"));
#endif
}

BEGIN_SHADER_PARAMETER_STRUCT(FRayTracingSceneBuildPassParams, )
	RDG_BUFFER_ACCESS(ScratchBuffer, ERHIAccess::UAVCompute)
	RDG_BUFFER_ACCESS(InstanceBuffer, ERHIAccess::SRVCompute)
	RDG_BUFFER_ACCESS(TLASBuffer, ERHIAccess::BVHWrite)

	RDG_BUFFER_ACCESS(DynamicGeometryScratchBuffer, ERHIAccess::UAVCompute)
END_SHADER_PARAMETER_STRUCT()

void FRayTracingScene::Build(FRDGBuilder& GraphBuilder, ERDGPassFlags ComputePassFlags, FRDGBufferRef DynamicGeometryScratchBuffer)
{
	const uint8 NumLayers = uint8(ERayTracingSceneLayer::NUM);
	
	for (uint32 LayerIndex = 0; LayerIndex < NumLayers; ++LayerIndex)
	{
		FLayer& Layer = Layers[LayerIndex];

		FRayTracingSceneBuildPassParams* PassParams = GraphBuilder.AllocParameters<FRayTracingSceneBuildPassParams>();
		PassParams->ScratchBuffer = Layer.BuildScratchBuffer;
		PassParams->InstanceBuffer = Layer.InstanceBuffer;
		PassParams->TLASBuffer = Layer.RayTracingSceneBufferRDG;
		PassParams->DynamicGeometryScratchBuffer = DynamicGeometryScratchBuffer; // TODO: Is this necessary?

		GraphBuilder.AddPass(RDG_EVENT_NAME("RayTracingBuildScene"), PassParams, ComputePassFlags,
			[PassParams, this, &Layer](FRHICommandList& RHICmdList)
			{
				FRayTracingSceneBuildParams BuildParams;
				BuildParams.Scene = Layer.RayTracingSceneRHI;
				BuildParams.ScratchBuffer = PassParams->ScratchBuffer->GetRHI();
				BuildParams.ScratchBufferOffset = 0;
				BuildParams.InstanceBuffer = PassParams->InstanceBuffer->GetRHI();
				BuildParams.InstanceBufferOffset = 0;
				BuildParams.NumInstances = Layer.InitializationData.NumNativeCPUInstances + Layer.InitializationData.NumNativeGPUSceneInstances;
				BuildParams.ReferencedGeometries = Layer.InitializationData.ReferencedGeometries;
				BuildParams.PerInstanceGeometries = Layer.InitializationData.PerInstanceGeometries;

				RHICmdList.BindAccelerationStructureMemory(Layer.RayTracingSceneRHI, PassParams->TLASBuffer->GetRHI(), 0);
				RHICmdList.BuildAccelerationStructure(BuildParams);
			});
	}
}

bool FRayTracingScene::IsCreated() const
{
	return bUsedThisFrame;
}

FRHIRayTracingScene* FRayTracingScene::GetRHIRayTracingScene(ERayTracingSceneLayer Layer) const
{
	return Layers[uint8(Layer)].RayTracingSceneRHI.GetReference();
}

FRHIRayTracingScene* FRayTracingScene::GetRHIRayTracingSceneChecked(ERayTracingSceneLayer Layer) const
{
	FRHIRayTracingScene* Result = GetRHIRayTracingScene(Layer);
	checkf(Result, TEXT("Ray tracing scene was not created. Perhaps Create() was not called."));
	return Result;
}

FShaderResourceViewRHIRef FRayTracingScene::CreateLayerViewRHI(FRHICommandListBase& RHICmdList, ERayTracingSceneLayer InLayer) const
{
	const FLayer& Layer = Layers[uint8(InLayer)];
	checkf(Layer.RayTracingScenePooledBuffer, TEXT("Ray tracing scene was not created. Perhaps Create() was not called."));
	return RHICmdList.CreateShaderResourceView(FShaderResourceViewInitializer(Layer.RayTracingScenePooledBuffer->GetRHI(), Layer.RayTracingSceneRHI, 0));
}

FRDGBufferSRVRef FRayTracingScene::GetLayerView(ERayTracingSceneLayer Layer) const
{
	checkf(Layers[uint8(Layer)].RayTracingSceneBufferSRV, TEXT("Ray tracing scene SRV was not created. Perhaps Create() was not called."));
	return Layers[uint8(Layer)].RayTracingSceneBufferSRV;
}

uint32 FRayTracingScene::GetNumNativeInstances(ERayTracingSceneLayer InLayer) const
{
	const FLayer& Layer = Layers[uint8(InLayer)];
	checkf(bInitializationDataBuilt, TEXT("Must call BuildInitializationData() or Create() before using GetNumNativeInstances()."));
	return Layer.InitializationData.NumNativeCPUInstances + Layer.InitializationData.NumNativeGPUSceneInstances;
}

FRayTracingScene::FInstanceHandle FRayTracingScene::AddInstance(FRayTracingGeometryInstance Instance, ERayTracingSceneLayer InLayer, const FPrimitiveSceneProxy* Proxy, bool bDynamic)
{
	FLayer& Layer = Layers[uint8(InLayer)];

	FRHIRayTracingGeometry* GeometryRHI = Instance.GeometryRHI;

	const uint32 InstanceIndex = Layer.Instances.Add(MoveTemp(Instance));

	if (bInstanceDebugDataEnabled)
	{
		FRayTracingInstanceDebugData& InstanceDebugData = Layer.InstancesDebugData.AddDefaulted_GetRef();
		InstanceDebugData.Flags = bDynamic ? 1 : 0;
		InstanceDebugData.GeometryAddress = uint64(GeometryRHI);

		if (Proxy)
		{
			InstanceDebugData.ProxyHash = Proxy->GetTypeHash();
		}

		check(Layer.Instances.Num() == Layer.InstancesDebugData.Num());
	}

	return { InLayer, InstanceIndex };
}

FRayTracingScene::FInstanceRange FRayTracingScene::AllocateInstanceRangeUninitialized(uint32 NumInstances, ERayTracingSceneLayer InLayer)
{
	FLayer& Layer = Layers[uint8(InLayer)];

	const uint32 OldNum = Layer.Instances.AddUninitialized(NumInstances);

	if (bInstanceDebugDataEnabled)
	{
		Layer.InstancesDebugData.AddUninitialized(NumInstances);

		check(Layer.Instances.Num() == Layer.InstancesDebugData.Num());
	}

	return { InLayer, OldNum, NumInstances };
}

void FRayTracingScene::SetInstance(FInstanceRange InstanceRange, uint32 InstanceIndexInRange, FRayTracingGeometryInstance InInstance, const FPrimitiveSceneProxy* Proxy, bool bDynamic)
{
	checkf(InstanceIndexInRange < InstanceRange.Num, TEXT("InstanceIndexInRange (%d) is out of bounds for the range (%d)"), InstanceIndexInRange, InstanceRange.Num);

	FLayer& Layer = Layers[uint8(InstanceRange.Layer)];

	const uint32 InstanceIndex = InstanceRange.StartIndex + InstanceIndexInRange;

	FRHIRayTracingGeometry* GeometryRHI = InInstance.GeometryRHI;

	FRayTracingGeometryInstance* Instance = &Layer.Instances[InstanceIndex];
	new (Instance) FRayTracingGeometryInstance(MoveTemp(InInstance));

	if (bInstanceDebugDataEnabled)
	{
		FRayTracingInstanceDebugData InstanceDebugData;
		InstanceDebugData.Flags = bDynamic ? 1 : 0;
		InstanceDebugData.GeometryAddress = uint64(GeometryRHI);

		if (Proxy)
		{
			InstanceDebugData.ProxyHash = Proxy->GetTypeHash();
		}

		Layer.InstancesDebugData[InstanceIndex] = InstanceDebugData;

		check(Layer.Instances.Num() == Layer.InstancesDebugData.Num());
	}
}

void FRayTracingScene::Reset(bool bInInstanceDebugDataEnabled)
{
	const uint8 NumLayers = uint8(ERayTracingSceneLayer::NUM);

	for (uint32 LayerIndex = 0; LayerIndex < NumLayers; ++LayerIndex)
	{
		FLayer& Layer = Layers[LayerIndex];

		Layer.Instances.Reset();
		Layer.InstancesDebugData.Reset();

		Layer.RayTracingSceneRHI = nullptr;
		Layer.RayTracingSceneBufferRDG = nullptr;
		Layer.RayTracingSceneBufferSRV = nullptr;

		Layer.InstanceBuffer = nullptr;
		Layer.BuildScratchBuffer = nullptr;
		Layer.InstanceDebugBuffer = nullptr;
		Layer.DebugInstanceGPUSceneIndexBuffer = nullptr;
	}

	CallableCommands.Reset();
	UniformBuffers.Reset();
	GeometriesToBuild.Reset();
	UsedCoarseMeshStreamingHandles.Reset();

	NumSegments = 0;
	NumMissShaderSlots = 1;
	NumCallableShaderSlots = 0;

	Allocator.Flush();

	bInstanceDebugDataEnabled = bInInstanceDebugDataEnabled;
}

void FRayTracingScene::EndFrame()
{
	Reset(false);

	// Release the resources if ray tracing wasn't used
	if (!bUsedThisFrame)
	{
		const uint8 NumLayers = uint8(ERayTracingSceneLayer::NUM);

		for (uint32 LayerIndex = 0; LayerIndex < NumLayers; ++LayerIndex)
		{
			Layers[LayerIndex] = {};
		}

		CallableCommands.Empty();
		UniformBuffers.Empty();
		GeometriesToBuild.Empty();
		UsedCoarseMeshStreamingHandles.Empty();

#if STATS
		ReleaseReadbackBuffers();

		StatsReadbackBuffersWriteIndex = 0;
		StatsReadbackBuffersNumPending = 0;

		NumActiveInstances = 0;
#endif
	}

	bUsedThisFrame = false;
	bInitializationDataBuilt = false;
}

void FRayTracingScene::ReleaseReadbackBuffers()
{
#if STATS
	for (auto& ReadbackBuffer : StatsReadbackBuffers)
	{
		delete ReadbackBuffer;
	}

	StatsReadbackBuffers.Empty();
#endif
}

#endif // RHI_RAYTRACING
