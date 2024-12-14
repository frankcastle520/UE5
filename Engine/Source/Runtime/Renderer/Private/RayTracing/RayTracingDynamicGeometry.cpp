// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshMaterialShader.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "ScenePrivate.h"
#include "RayTracingDynamicGeometryCollection.h"
#include "RayTracingInstance.h"
#include "RayTracingGeometry.h"
#include "RenderGraphBuilder.h"

#if RHI_RAYTRACING

#include "Materials/MaterialRenderProxy.h"

DECLARE_GPU_STAT(RayTracingDynamicGeometry);

DECLARE_DWORD_COUNTER_STAT(TEXT("Ray tracing dynamic build primitives"), STAT_RayTracingDynamicBuildPrimitives, STATGROUP_SceneRendering);
DECLARE_DWORD_COUNTER_STAT(TEXT("Ray tracing dynamic update primitives"), STAT_RayTracingDynamicUpdatePrimitives, STATGROUP_SceneRendering);

static int32 GRTDynGeomSharedVertexBufferSizeInMB = 4;
static FAutoConsoleVariableRef CVarRTDynGeomSharedVertexBufferSizeInMB(
	TEXT("r.RayTracing.DynamicGeometry.SharedVertexBufferSizeInMB"),
	GRTDynGeomSharedVertexBufferSizeInMB,
	TEXT("Size of the a single shared vertex buffer used during the BLAS update of dynamic geometries (default 4MB)"),
	ECVF_RenderThreadSafe
);

static int32 GRTDynGeomSharedVertexBufferGarbageCollectLatency = 30;
static FAutoConsoleVariableRef CVarRTDynGeomSharedVertexBufferGarbageCollectLatency(
	TEXT("r.RayTracing.DynamicGeometry.SharedVertexBufferGarbageCollectLatency"),
	GRTDynGeomSharedVertexBufferGarbageCollectLatency,
	TEXT("Amount of update cycles before a heap is deleted when not used (default 30)."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarRTDynGeomMaxUpdatePrimitivesPerFrame(
	TEXT("r.RayTracing.DynamicGeometry.MaxUpdatePrimitivesPerFrame"),
	-1,
	TEXT("Sets the dynamic ray tracing acceleration structure build budget in terms of maximum number of updated triangles per frame (<= 0 then disabled and all acceleration structures are updated - default)"),
	ECVF_RenderThreadSafe
);

class FRayTracingDynamicGeometryConverterCS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FRayTracingDynamicGeometryConverterCS, MeshMaterial);
public:
	FRayTracingDynamicGeometryConverterCS(const FMeshMaterialShaderType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{
		PassUniformBuffer.Bind(Initializer.ParameterMap, FSceneTextureUniformParameters::FTypeInfo::GetStructMetadata()->GetShaderVariableName());

		RWVertexPositions.Bind(Initializer.ParameterMap, TEXT("RWVertexPositions"));
		UsingIndirectDraw.Bind(Initializer.ParameterMap, TEXT("UsingIndirectDraw"));
		NumVertices.Bind(Initializer.ParameterMap, TEXT("NumVertices"));
		MinVertexIndex.Bind(Initializer.ParameterMap, TEXT("MinVertexIndex"));
		PrimitiveId.Bind(Initializer.ParameterMap, TEXT("PrimitiveId"));
		OutputVertexBaseIndex.Bind(Initializer.ParameterMap, TEXT("OutputVertexBaseIndex"));
		bApplyWorldPositionOffset.Bind(Initializer.ParameterMap, TEXT("bApplyWorldPositionOffset"));
		InstanceId.Bind(Initializer.ParameterMap, TEXT("InstanceId"));
		WorldToInstance.Bind(Initializer.ParameterMap, TEXT("WorldToInstance"));
	}

	FRayTracingDynamicGeometryConverterCS() = default;

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return Parameters.VertexFactoryType->SupportsRayTracingDynamicGeometry() && IsRayTracingEnabledForProject(Parameters.Platform) && RHISupportsRayTracing(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("SCENE_TEXTURES_DISABLED"), 1);
		OutEnvironment.SetDefine(TEXT("RAYTRACING_DYNAMIC_GEOMETRY_CONVERTER"), 1);
	}

	void GetShaderBindings(
		const FScene* Scene,
		ERHIFeatureLevel::Type FeatureLevel,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMaterialRenderProxy& MaterialRenderProxy,
		const FMaterial& Material,
		const FMeshMaterialShaderElementData& ShaderElementData,
		FMeshDrawSingleShaderBindings& ShaderBindings) const
	{
		FMeshMaterialShader::GetShaderBindings(Scene, FeatureLevel, PrimitiveSceneProxy, MaterialRenderProxy, Material, ShaderElementData, ShaderBindings);
	}

	void GetElementShaderBindings(
		const FShaderMapPointerTable& PointerTable,
		const FScene* Scene,
		const FSceneView* ViewIfDynamicMeshCommand,
		const FVertexFactory* VertexFactory,
		const EVertexInputStreamType InputStreamType,
		ERHIFeatureLevel::Type FeatureLevel,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMeshBatch& MeshBatch, 
		const FMeshBatchElement& BatchElement,
		const FMeshMaterialShaderElementData& ShaderElementData,
		FMeshDrawSingleShaderBindings& ShaderBindings,
		FVertexInputStreamArray& VertexStreams) const
	{
		FMeshMaterialShader::GetElementShaderBindings(PointerTable, Scene, ViewIfDynamicMeshCommand, VertexFactory, InputStreamType, FeatureLevel, PrimitiveSceneProxy, MeshBatch, BatchElement, ShaderElementData, ShaderBindings, VertexStreams);
	}

	LAYOUT_FIELD(FShaderResourceParameter, RWVertexPositions);
	LAYOUT_FIELD(FShaderParameter, UsingIndirectDraw);
	LAYOUT_FIELD(FShaderParameter, NumVertices);
	LAYOUT_FIELD(FShaderParameter, MinVertexIndex);
	LAYOUT_FIELD(FShaderParameter, PrimitiveId);
	LAYOUT_FIELD(FShaderParameter, bApplyWorldPositionOffset);
	LAYOUT_FIELD(FShaderParameter, OutputVertexBaseIndex);
	LAYOUT_FIELD(FShaderParameter, InstanceId);
	LAYOUT_FIELD(FShaderParameter, WorldToInstance);
};

IMPLEMENT_MATERIAL_SHADER_TYPE(, FRayTracingDynamicGeometryConverterCS, TEXT("/Engine/Private/RayTracing/RayTracingDynamicMesh.usf"), TEXT("RayTracingDynamicGeometryConverterCS"), SF_Compute);

FRayTracingDynamicGeometryCollection::FRayTracingDynamicGeometryCollection() 
{
}

FRayTracingDynamicGeometryCollection::~FRayTracingDynamicGeometryCollection()
{
	for (FVertexPositionBuffer* Buffer : VertexPositionBuffers)
	{
		delete Buffer;
	}
	VertexPositionBuffers.Empty();
}

void FRayTracingDynamicGeometryCollection::Clear()
{
	// Clear working arrays - keep max size allocated
	DispatchCommands.Empty(DispatchCommands.Max());
	BuildParams.Empty(BuildParams.Max());
	Segments.Empty(Segments.Max());

	DynamicGeometryBuilds.Empty(DynamicGeometryBuilds.Max());
	DynamicGeometryUpdates.Empty(DynamicGeometryUpdates.Max());
}

int64 FRayTracingDynamicGeometryCollection::BeginUpdate()
{
	check(DispatchCommands.IsEmpty());
	check(BuildParams.IsEmpty());
	check(Segments.IsEmpty());
	check(ReferencedUniformBuffers.IsEmpty());
	check(DynamicGeometryBuilds.IsEmpty());
	check(DynamicGeometryUpdates.IsEmpty());

	// Vertex buffer data can be immediatly reused the next frame, because it's already 'consumed' for building the AccelerationStructure data
	// Garbage collect unused buffers for n generations
	for (int32 BufferIndex = 0; BufferIndex < VertexPositionBuffers.Num(); ++BufferIndex)
	{
		FVertexPositionBuffer* Buffer = VertexPositionBuffers[BufferIndex];
		Buffer->UsedSize = 0;

		if (Buffer->LastUsedGenerationID + GRTDynGeomSharedVertexBufferGarbageCollectLatency <= SharedBufferGenerationID)
		{
			VertexPositionBuffers.RemoveAtSwap(BufferIndex);
			delete Buffer;
			BufferIndex--;
		}
	}

	// Increment generation ID used for validation
	SharedBufferGenerationID++;

	return SharedBufferGenerationID;
}

void FRayTracingDynamicGeometryCollection::AddDynamicMeshBatchForGeometryUpdate(
	const FScene* Scene,
	const FSceneView* View,
	const FPrimitiveSceneProxy* PrimitiveSceneProxy,
	const FRayTracingDynamicGeometryUpdateParams& UpdateParams,
	uint32 PrimitiveId
)
{
	AddDynamicMeshBatchForGeometryUpdate(FRHICommandListImmediate::Get(), Scene, View, PrimitiveSceneProxy, UpdateParams, PrimitiveId);
}

void FRayTracingDynamicGeometryCollection::AddDynamicMeshBatchForGeometryUpdate(
	FRHICommandListBase& RHICmdList,
	const FScene* Scene,
	const FSceneView* View,
	const FPrimitiveSceneProxy* PrimitiveSceneProxy,
	const FRayTracingDynamicGeometryUpdateParams& UpdateParams,
	uint32 PrimitiveId
)
{
	FRayTracingGeometry& Geometry = *UpdateParams.Geometry;
	bool bUsingIndirectDraw = UpdateParams.bUsingIndirectDraw;
	uint32 NumMaxVertices = UpdateParams.NumVertices;

	FRWBuffer* RWBuffer = UpdateParams.Buffer;
	uint32 VertexBufferOffset = 0;
	bool bUseSharedVertexBuffer = false;

	if (ReferencedUniformBuffers.Num() == 0 || ReferencedUniformBuffers.Last() != View->ViewUniformBuffer)
	{
		// Keep ViewUniformBuffer alive until EndUpdate()
		ReferencedUniformBuffers.Add(View->ViewUniformBuffer);
	}

	// If update params didn't provide a buffer then use a shared vertex position buffer
	if (RWBuffer == nullptr)
	{
		FVertexPositionBuffer* VertexPositionBuffer = nullptr;
		for (FVertexPositionBuffer* Buffer : VertexPositionBuffers)
		{
			if ((Buffer->RWBuffer.NumBytes - Buffer->UsedSize) >= UpdateParams.VertexBufferSize)
			{
				VertexPositionBuffer = Buffer;
				break;
			}
		}

		// Allocate a new buffer?
		if (VertexPositionBuffer == nullptr)
		{
			VertexPositionBuffer = new FVertexPositionBuffer;
			VertexPositionBuffers.Add(VertexPositionBuffer);

			static const uint32 VertexBufferCacheSize = GRTDynGeomSharedVertexBufferSizeInMB * 1024 * 1024;
			uint32 AllocationSize = FMath::Max(VertexBufferCacheSize, UpdateParams.VertexBufferSize);

			VertexPositionBuffer->RWBuffer.Initialize(RHICmdList, TEXT("FRayTracingDynamicGeometryCollection::RayTracingDynamicVertexBuffer"), sizeof(float), AllocationSize / sizeof(float), PF_R32_FLOAT, BUF_UnorderedAccess | BUF_ShaderResource);
			VertexPositionBuffer->UsedSize = 0;
		}

		// Update the last used generation ID
		VertexPositionBuffer->LastUsedGenerationID = SharedBufferGenerationID;

		// Get the offset and update used size
		VertexBufferOffset = VertexPositionBuffer->UsedSize;
		VertexPositionBuffer->UsedSize += UpdateParams.VertexBufferSize;

		// Make sure vertex buffer offset is aligned to 16 (required for Raw SRV views)
		VertexPositionBuffer->UsedSize = Align(VertexPositionBuffer->UsedSize, 16);

		bUseSharedVertexBuffer = true;
		RWBuffer = &VertexPositionBuffer->RWBuffer;
	}
	check(IsAligned(VertexBufferOffset, 16));

	FRayTracingDynamicGeometryBuildParams GeometryBuildParams;
	GeometryBuildParams.DispatchCommands.Reserve(UpdateParams.MeshBatches.Num());

	for (const FMeshBatch& MeshBatch : UpdateParams.MeshBatches)
	{
		if (!ensureMsgf(MeshBatch.VertexFactory->GetType()->SupportsRayTracingDynamicGeometry(),
			TEXT("FRayTracingDynamicGeometryConverterCS doesn't support %s. Skipping rendering of %s.  This can happen when the skinning cache runs out of space and falls back to GPUSkinVertexFactory."),
			MeshBatch.VertexFactory->GetType()->GetName(), *PrimitiveSceneProxy->GetOwnerName().ToString()))
		{
			continue;
		}

		const FMaterialRenderProxy* MaterialRenderProxyPtr = MeshBatch.MaterialRenderProxy;
		while (MaterialRenderProxyPtr)
		{
			const FMaterial* MaterialPtr = MaterialRenderProxyPtr->GetMaterialNoFallback(Scene->GetFeatureLevel());
			if (MaterialPtr && MaterialPtr->GetRenderingThreadShaderMap())
			{
				const FMaterial& Material = *MaterialPtr;
				const FMaterialRenderProxy& MaterialRenderProxy = *MaterialRenderProxyPtr;

				auto* MaterialInterface = Material.GetMaterialInterface();

				FMeshComputeDispatchCommand DispatchCmd;

				FMaterialShaderTypes ShaderTypes;
				ShaderTypes.AddShaderType<FRayTracingDynamicGeometryConverterCS>();

				FMaterialShaders MaterialShaders;
				if (Material.TryGetShaders(ShaderTypes, MeshBatch.VertexFactory->GetType(), MaterialShaders))
				{
					TShaderRef<FRayTracingDynamicGeometryConverterCS> Shader;
					MaterialShaders.TryGetShader(SF_Compute, Shader);

					FMeshProcessorShaders MeshProcessorShaders;
					MeshProcessorShaders.ComputeShader = Shader;

					DispatchCmd.MaterialShader = Shader;
					FMeshDrawShaderBindings& ShaderBindings = DispatchCmd.ShaderBindings;
					ShaderBindings.Initialize(MeshProcessorShaders);

					FMeshMaterialShaderElementData ShaderElementData;
					ShaderElementData.InitializeMeshMaterialData(View, PrimitiveSceneProxy, MeshBatch, -1, false);

					FMeshDrawSingleShaderBindings SingleShaderBindings = ShaderBindings.GetSingleShaderBindings(SF_Compute);
					Shader->GetShaderBindings(Scene, Scene->GetFeatureLevel(), PrimitiveSceneProxy, MaterialRenderProxy, Material, ShaderElementData, SingleShaderBindings);

					FVertexInputStreamArray DummyArray;
					FMeshMaterialShader::GetElementShaderBindings(Shader, Scene, View, MeshBatch.VertexFactory, EVertexInputStreamType::Default, Scene->GetFeatureLevel(), PrimitiveSceneProxy, MeshBatch, MeshBatch.Elements[0], ShaderElementData, SingleShaderBindings, DummyArray);

					DispatchCmd.TargetBuffer = RWBuffer;
					DispatchCmd.NumMaxVertices = UpdateParams.NumVertices;

					// Setup the loose parameters directly on the binding
					uint32 OutputVertexBaseIndex = VertexBufferOffset / sizeof(float);
					uint32 MinVertexIndex = MeshBatch.Elements[0].MinVertexIndex;
					uint32 NumCPUVertices = UpdateParams.NumVertices;
					if (MeshBatch.Elements[0].MinVertexIndex < MeshBatch.Elements[0].MaxVertexIndex)
					{
						NumCPUVertices = 1 + MeshBatch.Elements[0].MaxVertexIndex - MeshBatch.Elements[0].MinVertexIndex;
					}

					const uint32 VertexBufferNumElements = UpdateParams.VertexBufferSize / sizeof(FVector3f) - MinVertexIndex;
					if (!ensureMsgf(NumCPUVertices <= VertexBufferNumElements,
						TEXT("Vertex buffer contains %d vertices, but RayTracingDynamicGeometryConverterCS dispatch command expects at least %d."),
						VertexBufferNumElements, NumCPUVertices))
					{
						NumCPUVertices = VertexBufferNumElements;
					}

					SingleShaderBindings.Add(Shader->UsingIndirectDraw, bUsingIndirectDraw ? 1 : 0);
					SingleShaderBindings.Add(Shader->NumVertices, NumCPUVertices);
					SingleShaderBindings.Add(Shader->MinVertexIndex, MinVertexIndex);
					SingleShaderBindings.Add(Shader->PrimitiveId, PrimitiveId);
					SingleShaderBindings.Add(Shader->OutputVertexBaseIndex, OutputVertexBaseIndex);
					SingleShaderBindings.Add(Shader->bApplyWorldPositionOffset, UpdateParams.bApplyWorldPositionOffset ? 1 : 0);
					SingleShaderBindings.Add(Shader->InstanceId, UpdateParams.InstanceId);
					SingleShaderBindings.Add(Shader->WorldToInstance, UpdateParams.WorldToInstance);

#if MESH_DRAW_COMMAND_DEBUG_DATA
					ShaderBindings.Finalize(&MeshProcessorShaders);
#endif

					GeometryBuildParams.DispatchCommands.Add(DispatchCmd);

					break;
				}
			}

			MaterialRenderProxyPtr = MaterialRenderProxyPtr->GetFallback(Scene->GetFeatureLevel());
		}
	}

	bool bRefit = true;

	// Optionally resize the buffer when not shared (could also be lazy allocated and still empty)
	if (!bUseSharedVertexBuffer && RWBuffer->NumBytes != UpdateParams.VertexBufferSize)
	{
		RWBuffer->Initialize(RHICmdList, TEXT("FRayTracingDynamicGeometryCollection::RayTracingDynamicVertexBuffer"), sizeof(float), UpdateParams.VertexBufferSize / sizeof(float), PF_R32_FLOAT, BUF_UnorderedAccess | BUF_ShaderResource);
		bRefit = false;
	}

	if (!Geometry.IsValid() || Geometry.IsEvicted())
	{
		bRefit = false;
	}

	if (!Geometry.Initializer.bAllowUpdate)
	{
		bRefit = false;
	}

	check(Geometry.IsInitialized());

	if (Geometry.Initializer.TotalPrimitiveCount != UpdateParams.NumTriangles)
	{
		check(Geometry.Initializer.Segments.Num() <= 1);
		Geometry.Initializer.TotalPrimitiveCount = UpdateParams.NumTriangles;
		Geometry.Initializer.Segments.Empty();
		FRayTracingGeometrySegment Segment;
		Segment.NumPrimitives = UpdateParams.NumTriangles;
		Segment.MaxVertices = UpdateParams.NumVertices;
		Geometry.Initializer.Segments.Add(Segment);
		bRefit = false;
	}

	for (FRayTracingGeometrySegment& Segment : Geometry.Initializer.Segments)
	{
		Segment.VertexBuffer = RWBuffer->Buffer;
		Segment.VertexBufferOffset = VertexBufferOffset;
	}

	if (!bRefit)
	{
		checkf(Geometry.RawData.IsEmpty() && Geometry.Initializer.OfflineData == nullptr, TEXT("Dynamic geometry is not expected to have offline acceleration structure data"));
		Geometry.CreateRayTracingGeometry(RHICmdList, ERTAccelerationStructureBuildPriority::Skip);
	}

	EAccelerationStructureBuildMode BuildMode = Geometry.GetRequiresBuild()
		? EAccelerationStructureBuildMode::Build
		: EAccelerationStructureBuildMode::Update;

	GeometryBuildParams.Geometry = UpdateParams.Geometry;

	if (bUseSharedVertexBuffer)
	{
		GeometryBuildParams.SegmentOffset = Segments.Num();
		Segments.Append(Geometry.Initializer.Segments);
	}

	Geometry.SetRequiresBuild(false);

	if (BuildMode == EAccelerationStructureBuildMode::Build)
	{
		DynamicGeometryBuilds.Add(GeometryBuildParams);
	}
	else
	{
		DynamicGeometryUpdates.Add(GeometryBuildParams);
	}
	
	if (bUseSharedVertexBuffer)
	{
		Geometry.DynamicGeometrySharedBufferGenerationID = SharedBufferGenerationID;
	}
	else
	{
		Geometry.DynamicGeometrySharedBufferGenerationID = FRayTracingGeometry::NonSharedVertexBuffers;
	}
}

BEGIN_SHADER_PARAMETER_STRUCT(FRayTracingDynamicGeometryUpdatePassParams, )
	RDG_BUFFER_ACCESS(DynamicGeometryScratchBuffer, ERHIAccess::UAVCompute)

	SHADER_PARAMETER_STRUCT_INCLUDE(FViewShaderParameters, View)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneUniformParameters, Scene)
END_SHADER_PARAMETER_STRUCT()

uint32 FRayTracingDynamicGeometryCollection::Update()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FRayTracingDynamicGeometryCollection::Update);

	const int32 TotalNumGeometryBuilds = DynamicGeometryBuilds.Num() + DynamicGeometryUpdates.Num();
	if (TotalNumGeometryBuilds == 0)
	{
		return 0;
	}

	checkf(DispatchCommands.IsEmpty(), TEXT("DispatchCommands is not empty. Previous frame updates were not dispatched."));
	checkf(BuildParams.IsEmpty(), TEXT("BuildParams is not empty. Previous frame updates were not dispatched."));

	DispatchCommands.Reserve(TotalNumGeometryBuilds);
	BuildParams.Reserve(TotalNumGeometryBuilds);

	FRayTracingGeometrySegment* SegmentData = Segments.GetData();

	const uint32 ScratchAlignment = GRHIRayTracingScratchBufferAlignment;

	uint32 BLASScratchSize = 0;
	int32 NumBuildPrimitives = 0;

	for (const FRayTracingDynamicGeometryBuildParams& Build : DynamicGeometryBuilds)
	{
		FRHIRayTracingGeometry* RayTracingGeometry = Build.Geometry->GetRHI();

		NumBuildPrimitives += Build.Geometry->Initializer.TotalPrimitiveCount;

		const uint32 ScratchSize = RayTracingGeometry->GetSizeInfo().BuildScratchSize;
		BLASScratchSize = Align(BLASScratchSize + ScratchSize, ScratchAlignment);

		FRayTracingGeometryBuildParams BuildParam;
		BuildParam.Geometry = RayTracingGeometry;
		BuildParam.BuildMode = EAccelerationStructureBuildMode::Build;

		if (Build.SegmentOffset >= 0)
		{
			BuildParam.Segments = MakeArrayView(&SegmentData[Build.SegmentOffset], Build.Geometry->Initializer.Segments.Num());
		}

		BuildParams.Add(MoveTemp(BuildParam));

		DispatchCommands.Append(Build.DispatchCommands);
	}

	const int32 MaxUpdatePrimitivesPerFrame = CVarRTDynGeomMaxUpdatePrimitivesPerFrame.GetValueOnRenderThread();

	int32 NumUpdatedPrimitives = 0;

	if (MaxUpdatePrimitivesPerFrame <= 0)
	{
		for (const FRayTracingDynamicGeometryBuildParams& Update : DynamicGeometryUpdates)
		{
			FRHIRayTracingGeometry* RayTracingGeometry = Update.Geometry->GetRHI();

			Update.Geometry->LastUpdatedFrame = GFrameCounterRenderThread;

			NumUpdatedPrimitives += Update.Geometry->Initializer.TotalPrimitiveCount;

			const uint32 ScratchSize = RayTracingGeometry->GetSizeInfo().UpdateScratchSize;
			BLASScratchSize = Align(BLASScratchSize + ScratchSize, ScratchAlignment);

			FRayTracingGeometryBuildParams BuildParam;
			BuildParam.Geometry = RayTracingGeometry;
			BuildParam.BuildMode = EAccelerationStructureBuildMode::Update;
			if (Update.SegmentOffset >= 0)
			{
				BuildParam.Segments = MakeArrayView(&SegmentData[Update.SegmentOffset], Update.Geometry->Initializer.Segments.Num());
			}
			BuildParams.Add(MoveTemp(BuildParam));

			DispatchCommands.Append(Update.DispatchCommands);
		}
	}
	else
	{
		DynamicGeometryUpdates.Sort([](const FRayTracingDynamicGeometryBuildParams& InLHS, const FRayTracingDynamicGeometryBuildParams& InRHS)
			{
				return InLHS.Geometry->LastUpdatedFrame < InRHS.Geometry->LastUpdatedFrame;
			});

		for (const FRayTracingDynamicGeometryBuildParams& Update : DynamicGeometryUpdates)
		{
			FRHIRayTracingGeometry* RayTracingGeometry = Update.Geometry->GetRHI();

			Update.Geometry->LastUpdatedFrame = GFrameCounterRenderThread;

			NumUpdatedPrimitives += Update.Geometry->Initializer.TotalPrimitiveCount;

			const uint32 ScratchSize = RayTracingGeometry->GetSizeInfo().UpdateScratchSize;
			BLASScratchSize = Align(BLASScratchSize + ScratchSize, ScratchAlignment);

			FRayTracingGeometryBuildParams BuildParam;
			BuildParam.Geometry = RayTracingGeometry;
			BuildParam.BuildMode = EAccelerationStructureBuildMode::Update;
			if (Update.SegmentOffset >= 0)
			{
				BuildParam.Segments = MakeArrayView(&SegmentData[Update.SegmentOffset], Update.Geometry->Initializer.Segments.Num());
			}
			BuildParams.Add(MoveTemp(BuildParam));

			DispatchCommands.Append(Update.DispatchCommands);

			if (NumUpdatedPrimitives > MaxUpdatePrimitivesPerFrame)
			{
				break;
			}
		}
	}

	INC_DWORD_STAT_BY(STAT_RayTracingDynamicUpdatePrimitives, NumUpdatedPrimitives);
	INC_DWORD_STAT_BY(STAT_RayTracingDynamicBuildPrimitives, NumBuildPrimitives);

	return BLASScratchSize;
}

void FRayTracingDynamicGeometryCollection::AddDynamicGeometryUpdatePass(const FViewInfo& View, FRDGBuilder& GraphBuilder, ERDGPassFlags ComputePassFlags, FRDGBufferRef& OutDynamicGeometryScratchBuffer)
{
	RDG_GPU_MASK_SCOPE(GraphBuilder, FRHIGPUMask::All());
	RDG_EVENT_SCOPE_STAT(GraphBuilder, RayTracingDynamicGeometry, "RayTracingDynamicGeometry");
	RDG_GPU_STAT_SCOPE(GraphBuilder, RayTracingDynamicGeometry);

	const uint32 ScratchAlignment = GRHIRayTracingScratchBufferAlignment;
	const uint32 BLASScratchSize = Update();

	if (BLASScratchSize > 0)
	{
		FRDGBufferDesc ScratchBufferDesc;
		ScratchBufferDesc.Usage = EBufferUsageFlags::RayTracingScratch | EBufferUsageFlags::StructuredBuffer;
		ScratchBufferDesc.BytesPerElement = ScratchAlignment;
		ScratchBufferDesc.NumElements = FMath::DivideAndRoundUp(BLASScratchSize, ScratchAlignment);

		OutDynamicGeometryScratchBuffer = GraphBuilder.CreateBuffer(ScratchBufferDesc, TEXT("DynamicGeometry.BLASSharedScratchBuffer"));
	}

	FRayTracingDynamicGeometryUpdatePassParams* PassParams = GraphBuilder.AllocParameters<FRayTracingDynamicGeometryUpdatePassParams>();
	PassParams->View = View.GetShaderParameters();
	PassParams->Scene = View.GetSceneUniforms().GetBuffer(GraphBuilder);
	PassParams->DynamicGeometryScratchBuffer = OutDynamicGeometryScratchBuffer;	

	GraphBuilder.AddPass(RDG_EVENT_NAME("RayTracingDynamicUpdate"), PassParams, ComputePassFlags | ERDGPassFlags::NeverCull,
		[this, PassParams](FRDGAsyncTask, FRHICommandList& RHICmdList)
		{
			FRHIBuffer* DynamicGeometryScratchBuffer = PassParams->DynamicGeometryScratchBuffer ? PassParams->DynamicGeometryScratchBuffer->GetRHI() : nullptr;

			PRAGMA_DISABLE_DEPRECATION_WARNINGS
			DispatchUpdates(RHICmdList, DynamicGeometryScratchBuffer);
			EndUpdate();
			PRAGMA_ENABLE_DEPRECATION_WARNINGS
		});
}

void FRayTracingDynamicGeometryCollection::DispatchUpdates(FRHICommandList& RHICmdList, FRHIBuffer* ScratchBuffer)
{
	if (DispatchCommands.Num() > 0)
	{
		SCOPED_DRAW_EVENT(RHICmdList, RayTracingDynamicGeometryUpdate);

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(SortDispatchCommands);

			// This can be optimized by using sorted insert or using map on shaders
			// There are only a handful of unique shaders and a few target buffers so we want to swap state as little as possible
			// to reduce RHI thread overhead
			DispatchCommands.Sort([](const FMeshComputeDispatchCommand& InLHS, const FMeshComputeDispatchCommand& InRHS)
				{
					if (InLHS.MaterialShader.GetComputeShader() != InRHS.MaterialShader.GetComputeShader())
						return InLHS.MaterialShader.GetComputeShader() < InRHS.MaterialShader.GetComputeShader();

					return InLHS.TargetBuffer < InRHS.TargetBuffer;
				});
		}

		FMemMark Mark(FMemStack::Get());

		TArray<FRHITransitionInfo, TMemStackAllocator<>> TransitionsBefore, TransitionsAfter;
		TArray<FRHIUnorderedAccessView*, TMemStackAllocator<>> OverlapUAVs;
		TransitionsBefore.Reserve(DispatchCommands.Num());
		TransitionsAfter.Reserve(DispatchCommands.Num());
		OverlapUAVs.Reserve(DispatchCommands.Num());
		const FRWBuffer* LastBuffer = nullptr;
		TSet<const FRWBuffer*> TransitionedBuffers;
		for (FMeshComputeDispatchCommand& Cmd : DispatchCommands)
		{
			if (Cmd.TargetBuffer == nullptr)
			{
				continue;
			}
			FRHIUnorderedAccessView* UAV = Cmd.TargetBuffer->UAV.GetReference();

			// The list is sorted by TargetBuffer, so we can remove duplicates by simply looking at the previous value we've processed.
			if (LastBuffer == Cmd.TargetBuffer)
			{
				// This UAV is used by more than one dispatch, so tell the RHI it's OK to overlap the dispatches, because
				// we're updating disjoint regions.
				if (OverlapUAVs.Num() == 0 || OverlapUAVs.Last() != UAV)
				{
					OverlapUAVs.Add(UAV);
				}
				continue;
			}

			LastBuffer = Cmd.TargetBuffer;

			// In case different shaders use different TargetBuffer we want to add transition only once
			bool bAlreadyInSet = false;
			TransitionedBuffers.FindOrAdd(LastBuffer, &bAlreadyInSet);
			if (!bAlreadyInSet)
			{
				// Looks like the resource can get here in either UAVCompute or SRVMask mode, so we'll have to use Unknown until we can have better tracking.
				TransitionsBefore.Add(FRHITransitionInfo(UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));
				TransitionsAfter.Add(FRHITransitionInfo(UAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask));
			}
		}

		{
			FRHIComputeShader* CurrentShader = nullptr;
			FRWBuffer* CurrentBuffer = nullptr;

			// Transition to writeable for each cmd list and enable UAV overlap, because several dispatches can update non-overlapping portions of the same buffer.
			RHICmdList.Transition(TransitionsBefore);
			RHICmdList.BeginUAVOverlap(OverlapUAVs);

			// Cache the bound uniform buffers because a lot are the same between dispatches
			FShaderBindingState ShaderBindingState;

			for (FMeshComputeDispatchCommand& Cmd : DispatchCommands)
			{
				const TShaderRef<FRayTracingDynamicGeometryConverterCS>& Shader = Cmd.MaterialShader;
				FRHIComputeShader* ComputeShader = Shader.GetComputeShader();
				if (CurrentShader != ComputeShader)
				{
					SetComputePipelineState(RHICmdList, ComputeShader);
					CurrentBuffer = nullptr;
					CurrentShader = ComputeShader;

					// Reset binding state
					ShaderBindingState = FShaderBindingState();
				}

				FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();

				FRWBuffer* TargetBuffer = Cmd.TargetBuffer;
				if (CurrentBuffer != TargetBuffer)
				{
					CurrentBuffer = TargetBuffer;

					SetUAVParameter(BatchedParameters, Shader->RWVertexPositions, Cmd.TargetBuffer->UAV);
				}

				Cmd.ShaderBindings.SetParameters(BatchedParameters, &ShaderBindingState);
				RHICmdList.SetBatchedShaderParameters(CurrentShader, BatchedParameters);

				RHICmdList.DispatchComputeShader(FMath::DivideAndRoundUp<uint32>(Cmd.NumMaxVertices, 64), 1, 1);
			}

			// Make sure buffers are readable again and disable UAV overlap.
			RHICmdList.EndUAVOverlap(OverlapUAVs);
			RHICmdList.Transition(TransitionsAfter);
		}

		if (BuildParams.Num() > 0)
		{
			// Can't use parallel command list because we have to make sure we are not building BVH data
			// on the same RTGeometry on multiple threads at the same time. Ideally move the build
			// requests over to the RaytracingGeometry manager so they can be correctly scheduled
			// with other build requests in the engine (see UE-106982)
			SCOPED_DRAW_EVENT(RHICmdList, Build);

			FRHIBufferRange ScratchBufferRange;
			ScratchBufferRange.Buffer = ScratchBuffer;
			ScratchBufferRange.Offset = 0;
			RHICmdList.BuildAccelerationStructures(BuildParams, ScratchBufferRange);
		}
	}
}

void FRayTracingDynamicGeometryCollection::EndUpdate()
{
	ReferencedUniformBuffers.Empty(ReferencedUniformBuffers.Max());

	Clear();
}

uint32 FRayTracingDynamicGeometryCollection::ComputeScratchBufferSize()
{	
	return Update();
}

#undef USE_RAY_TRACING_DYNAMIC_GEOMETRY_PARALLEL_COMMAND_LISTS

#endif // RHI_RAYTRACING
