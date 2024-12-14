// Copyright Epic Games, Inc. All Rights Reserved.

#include "RayTracingInstanceBufferUtil.h"

#include "Lumen/Lumen.h"

#include "RayTracingDefinitions.h"
#include "GPUScene.h"

#include "RenderGraphBuilder.h"
#include "ShaderParameterUtils.h"
#include "RendererInterface.h"
#include "RenderCore.h"
#include "ShaderParameterStruct.h"
#include "GlobalShader.h"
#include "PipelineStateCache.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "ShaderCompilerCore.h"

#include "SceneRendering.h"

#include "Async/ParallelFor.h"

#include "Experimental/Containers/SherwoodHashTable.h"

#if RHI_RAYTRACING

FRayTracingSceneInitializationData BuildRayTracingSceneInitializationData(TConstArrayView<FRayTracingGeometryInstance> Instances)
{
	const uint32 NumSceneInstances = Instances.Num();
	
	FRayTracingSceneInitializationData Output;
	Output.NumNativeGPUSceneInstances = 0;
	Output.NumNativeCPUInstances = 0;
	Output.TotalNumSegments = 0;
	Output.InstanceGeometryIndices.SetNumUninitialized(NumSceneInstances);
	Output.BaseUploadBufferOffsets.SetNumUninitialized(NumSceneInstances);
	Output.BaseInstancePrefixSum.SetNumUninitialized(NumSceneInstances);
	Output.PerInstanceGeometries.SetNumUninitialized(NumSceneInstances);

	Experimental::TSherwoodMap<FRHIRayTracingGeometry*, uint32> UniqueGeometries;

	// Compute geometry segment prefix sum used by GetHitRecordBaseIndex() during resource binding

	uint32 NumNativeInstances = 0;

	for (uint32 InstanceIndex = 0; InstanceIndex < NumSceneInstances; ++InstanceIndex)
	{
		const FRayTracingGeometryInstance& InstanceDesc = Instances[InstanceIndex];

		const bool bGpuSceneInstance = InstanceDesc.BaseInstanceSceneDataOffset != -1 || !InstanceDesc.InstanceSceneDataOffsets.IsEmpty();
		const bool bCpuInstance = !bGpuSceneInstance;

		checkf(!bGpuSceneInstance || InstanceDesc.BaseInstanceSceneDataOffset != -1 || InstanceDesc.NumTransforms <= uint32(InstanceDesc.InstanceSceneDataOffsets.Num()),
			TEXT("Expected at least %d ray tracing geometry instance scene data offsets, but got %d."),
			InstanceDesc.NumTransforms, InstanceDesc.InstanceSceneDataOffsets.Num());
		checkf(!bCpuInstance || InstanceDesc.NumTransforms <= uint32(InstanceDesc.Transforms.Num()),
			TEXT("Expected at least %d ray tracing geometry instance transforms, but got %d."),
			InstanceDesc.NumTransforms, InstanceDesc.Transforms.Num());

		checkf(InstanceDesc.GeometryRHI, TEXT("Ray tracing instance must have a valid geometry."));

		Output.PerInstanceGeometries[InstanceIndex] = InstanceDesc.GeometryRHI;

		Output.TotalNumSegments += InstanceDesc.GeometryRHI->GetNumSegments();

		uint32 GeometryIndex = UniqueGeometries.FindOrAdd(InstanceDesc.GeometryRHI, Output.ReferencedGeometries.Num());
		Output.InstanceGeometryIndices[InstanceIndex] = GeometryIndex;
		if (GeometryIndex == Output.ReferencedGeometries.Num())
		{
			Output.ReferencedGeometries.Add(InstanceDesc.GeometryRHI);
		}

		if (bGpuSceneInstance)
		{
			check(InstanceDesc.Transforms.IsEmpty());
			Output.BaseUploadBufferOffsets[InstanceIndex] = Output.NumNativeGPUSceneInstances;
			Output.NumNativeGPUSceneInstances += InstanceDesc.NumTransforms;
		}
		else if (bCpuInstance)
		{
			Output.BaseUploadBufferOffsets[InstanceIndex] = Output.NumNativeCPUInstances;
			Output.NumNativeCPUInstances += InstanceDesc.NumTransforms;
		}
		else
		{
			checkNoEntry();
		}
		
		Output.BaseInstancePrefixSum[InstanceIndex] = NumNativeInstances;

		NumNativeInstances += InstanceDesc.NumTransforms;
	}

	return MoveTemp(Output);
}

FRayTracingSceneWithGeometryInstances CreateRayTracingSceneWithGeometryInstances(
	TConstArrayView<FRayTracingGeometryInstance> Instances,
	uint8 NumLayers,
	uint32 NumShaderSlotsPerGeometrySegment,
	uint32 NumMissShaderSlots,
	uint32 NumCallableShaderSlots,
	ERayTracingAccelerationStructureFlags BuildFlags)
{
	const uint32 NumSceneInstances = Instances.Num();

	FRayTracingSceneWithGeometryInstances Output;
	Output.NumNativeGPUSceneInstances = 0;
	Output.NumNativeCPUInstances = 0;
	Output.TotalNumSegments = 0;
	Output.InstanceGeometryIndices.SetNumUninitialized(NumSceneInstances);
	Output.BaseUploadBufferOffsets.SetNumUninitialized(NumSceneInstances);
	Output.BaseInstancePrefixSum.SetNumUninitialized(NumSceneInstances);
	Output.PerInstanceGeometries.SetNumUninitialized(NumSceneInstances);

	FRayTracingSceneInitializer Initializer;
	Initializer.DebugName = FName(TEXT("FRayTracingScene"));
	Initializer.BuildFlags = BuildFlags;

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	Initializer.BaseInstancePrefixSum.SetNumUninitialized(NumSceneInstances);
	Initializer.SegmentPrefixSum.SetNumUninitialized(NumSceneInstances);
	Initializer.NumNativeInstancesPerLayer.SetNumZeroed(NumLayers);
	Initializer.ShaderSlotsPerGeometrySegment = NumShaderSlotsPerGeometrySegment;
	Initializer.NumMissShaderSlots = NumMissShaderSlots;
	Initializer.NumCallableShaderSlots = NumCallableShaderSlots;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	Experimental::TSherwoodMap<FRHIRayTracingGeometry*, uint32> UniqueGeometries;

	// Compute geometry segment prefix sum used by GetHitRecordBaseIndex() during resource binding

	for (uint32 InstanceIndex = 0; InstanceIndex < NumSceneInstances; ++InstanceIndex)
	{
		const FRayTracingGeometryInstance& InstanceDesc = Instances[InstanceIndex];

		const bool bGpuSceneInstance = InstanceDesc.BaseInstanceSceneDataOffset != -1 || !InstanceDesc.InstanceSceneDataOffsets.IsEmpty();
		const bool bCpuInstance = !bGpuSceneInstance;

		checkf(!bGpuSceneInstance || InstanceDesc.BaseInstanceSceneDataOffset != -1 || InstanceDesc.NumTransforms <= uint32(InstanceDesc.InstanceSceneDataOffsets.Num()),
			TEXT("Expected at least %d ray tracing geometry instance scene data offsets, but got %d."),
			InstanceDesc.NumTransforms, InstanceDesc.InstanceSceneDataOffsets.Num());
		checkf(!bCpuInstance || InstanceDesc.NumTransforms <= uint32(InstanceDesc.Transforms.Num()),
			TEXT("Expected at least %d ray tracing geometry instance transforms, but got %d."),
			InstanceDesc.NumTransforms, InstanceDesc.Transforms.Num());

		checkf(InstanceDesc.GeometryRHI, TEXT("Ray tracing instance must have a valid geometry."));

		Output.PerInstanceGeometries[InstanceIndex] = InstanceDesc.GeometryRHI;

		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		// Compute geometry segment count prefix sum to be later used in GetHitRecordBaseIndex()
		Initializer.SegmentPrefixSum[InstanceIndex] = Output.TotalNumSegments;
		PRAGMA_ENABLE_DEPRECATION_WARNINGS

		Output.TotalNumSegments += InstanceDesc.GeometryRHI->GetNumSegments();

		uint32 GeometryIndex = UniqueGeometries.FindOrAdd(InstanceDesc.GeometryRHI, Output.ReferencedGeometries.Num());
		Output.InstanceGeometryIndices[InstanceIndex] = GeometryIndex;
		if (GeometryIndex == Output.ReferencedGeometries.Num())
		{
			Output.ReferencedGeometries.Add(InstanceDesc.GeometryRHI);
		}

		if (bGpuSceneInstance)
		{
			check(InstanceDesc.Transforms.IsEmpty());
			Output.BaseUploadBufferOffsets[InstanceIndex] = Output.NumNativeGPUSceneInstances;
			Output.NumNativeGPUSceneInstances += InstanceDesc.NumTransforms;
		}
		else if (bCpuInstance)
		{
			Output.BaseUploadBufferOffsets[InstanceIndex] = Output.NumNativeCPUInstances;
			Output.NumNativeCPUInstances += InstanceDesc.NumTransforms;
		}
		else
		{
			checkNoEntry();
		}

		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		checkf(InstanceDesc.LayerIndex < NumLayers, 
			TEXT("FRayTracingGeometryInstance is assigned to layer %d but raytracing scene being created only has %d layers."),
			InstanceDesc.LayerIndex, NumLayers);

		Initializer.BaseInstancePrefixSum[InstanceIndex] = Initializer.NumNativeInstancesPerLayer[InstanceDesc.LayerIndex];

		// Can't support same instance in multiple layers because BaseInstancePrefixSum would be different per layer
		Output.BaseInstancePrefixSum[InstanceIndex] = Initializer.NumNativeInstancesPerLayer[InstanceDesc.LayerIndex];

		Initializer.NumNativeInstancesPerLayer[InstanceDesc.LayerIndex] += InstanceDesc.NumTransforms;
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	Initializer.NumTotalSegments = Output.TotalNumSegments;

	if(NumLayers == 1)
	{
		Initializer.MaxNumInstances = Initializer.NumNativeInstancesPerLayer[0];
		Initializer.NumNativeInstancesPerLayer.Empty();
	}
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	Output.Scene = RHICreateRayTracingScene(MoveTemp(Initializer));

	return MoveTemp(Output);
}

void FillRayTracingInstanceUploadBuffer(
	FRayTracingSceneRHIRef RayTracingSceneRHI,
	FVector PreViewTranslation,
	TConstArrayView<FRayTracingGeometryInstance> Instances,
	TConstArrayView<uint32> InstanceGeometryIndices,
	TConstArrayView<uint32> BaseUploadBufferOffsets,
	TConstArrayView<uint32> BaseInstancePrefixSum,
	uint32 NumNativeGPUSceneInstances,
	uint32 NumNativeCPUInstances,
	TArrayView<FRayTracingInstanceDescriptorInput> OutInstanceUploadData,
	TArrayView<FVector4f> OutTransformData)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FillRayTracingInstanceUploadBuffer);

	const int32 NumSceneInstances = Instances.Num();
	const int32 MinBatchSize = 128;
	ParallelFor(
		TEXT("FillRayTracingInstanceUploadBuffer_Parallel"),
		NumSceneInstances,
		MinBatchSize,
		[
			OutInstanceUploadData,
			OutTransformData,
			NumNativeGPUSceneInstances,
			NumNativeCPUInstances,
			Instances,
			InstanceGeometryIndices,
			BaseUploadBufferOffsets,
			BaseInstancePrefixSum,
			PreViewTranslation
		](int32 SceneInstanceIndex)
		{
			const FRayTracingGeometryInstance& SceneInstance = Instances[SceneInstanceIndex];

			const uint32 NumTransforms = SceneInstance.NumTransforms;

			checkf(SceneInstance.UserData.Num() == 0 || SceneInstance.UserData.Num() >= int32(NumTransforms),
				TEXT("User data array must be either be empty (Instance.DefaultUserData is used), or contain one entry per entry in Transforms array."));			

			const bool bUseUniqueUserData = SceneInstance.UserData.Num() != 0;

			const bool bGpuSceneInstance = SceneInstance.BaseInstanceSceneDataOffset != -1 || !SceneInstance.InstanceSceneDataOffsets.IsEmpty();
			const bool bCpuInstance = !bGpuSceneInstance;

			checkf(bGpuSceneInstance + bCpuInstance == 1, TEXT("Instance can only get transforms from one of GPUScene, or Transforms array."));

			const uint32 AccelerationStructureIndex = InstanceGeometryIndices[SceneInstanceIndex];
			const uint32 BaseInstanceIndex = BaseInstancePrefixSum[SceneInstanceIndex];
			const uint32 BaseTransformIndex = bCpuInstance ? BaseUploadBufferOffsets[SceneInstanceIndex] : 0;

			uint32 BaseDescriptorIndex = BaseUploadBufferOffsets[SceneInstanceIndex];

			// Upload buffer is split into 2 sections [GPUSceneInstances][CPUInstances]
			if (!bGpuSceneInstance)
			{
				BaseDescriptorIndex += NumNativeGPUSceneInstances;
			}

			for (uint32 TransformIndex = 0; TransformIndex < NumTransforms; ++TransformIndex)
			{
				FRayTracingInstanceDescriptorInput InstanceDesc;

				if (bGpuSceneInstance)
				{
					if (SceneInstance.BaseInstanceSceneDataOffset != -1)
					{
						InstanceDesc.GPUSceneInstanceOrTransformIndex = SceneInstance.BaseInstanceSceneDataOffset + TransformIndex;
					}
					else
					{
						InstanceDesc.GPUSceneInstanceOrTransformIndex = SceneInstance.InstanceSceneDataOffsets[TransformIndex];
					}
				}
				else
				{
					InstanceDesc.GPUSceneInstanceOrTransformIndex = BaseTransformIndex + TransformIndex;
				}

				uint32 UserData;

				if (bUseUniqueUserData)
				{
					UserData = SceneInstance.UserData[TransformIndex];
				}
				else
				{
					UserData = SceneInstance.DefaultUserData;

					if (SceneInstance.bIncrementUserDataPerInstance)
					{
						UserData += TransformIndex;
					}
				}

				InstanceDesc.OutputDescriptorIndex = BaseInstanceIndex + TransformIndex;
				InstanceDesc.AccelerationStructureIndex = AccelerationStructureIndex;
				InstanceDesc.InstanceId = UserData;
				InstanceDesc.InstanceMaskAndFlags = SceneInstance.Mask | ((uint32)SceneInstance.Flags << 8);
				InstanceDesc.InstanceContributionToHitGroupIndex = SceneInstance.InstanceContributionToHitGroupIndex;
				InstanceDesc.bApplyLocalBoundsTransform = SceneInstance.bApplyLocalBoundsTransform;

				checkf(InstanceDesc.InstanceId <= 0xFFFFFF, TEXT("InstanceId must fit in 24 bits."));
				checkf(InstanceDesc.InstanceContributionToHitGroupIndex <= 0xFFFFFF, TEXT("InstanceContributionToHitGroupIndex must fit in 24 bits."));

				if (bCpuInstance)
				{
					const uint32 TransformDataOffset = InstanceDesc.GPUSceneInstanceOrTransformIndex * 3;
					FMatrix LocalToTranslatedWorld = SceneInstance.Transforms[TransformIndex].ConcatTranslation(PreViewTranslation);
					const FMatrix44f LocalToTranslatedWorldF = FMatrix44f(LocalToTranslatedWorld.GetTransposed());
					OutTransformData[TransformDataOffset + 0] = *(FVector4f*)&LocalToTranslatedWorldF.M[0];
					OutTransformData[TransformDataOffset + 1] = *(FVector4f*)&LocalToTranslatedWorldF.M[1];
					OutTransformData[TransformDataOffset + 2] = *(FVector4f*)&LocalToTranslatedWorldF.M[2];
				}

				OutInstanceUploadData[BaseDescriptorIndex + TransformIndex] = InstanceDesc;
			}
		});
}

void FillRayTracingInstanceUploadBuffer(
	FRayTracingSceneRHIRef RayTracingSceneRHI,
	FVector PreViewTranslation,
	TConstArrayView<FRayTracingGeometryInstance> Instances,
	TConstArrayView<uint32> InstanceGeometryIndices,
	TConstArrayView<uint32> BaseUploadBufferOffsets,
	uint32 NumNativeGPUSceneInstances,
	uint32 NumNativeCPUInstances,
	TArrayView<FRayTracingInstanceDescriptorInput> OutInstanceUploadData,
	TArrayView<FVector4f> OutTransformData)
{
	const FRayTracingSceneInitializer& SceneInitializer = RayTracingSceneRHI->GetInitializer();

	FillRayTracingInstanceUploadBuffer(
		RayTracingSceneRHI,
		PreViewTranslation,
		Instances,
		InstanceGeometryIndices,
		BaseUploadBufferOffsets,
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
		SceneInitializer.BaseInstancePrefixSum,
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
		NumNativeGPUSceneInstances,
		NumNativeCPUInstances,
		OutInstanceUploadData,
		OutTransformData);
}

struct FRayTracingBuildInstanceBufferCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FRayTracingBuildInstanceBufferCS);
	SHADER_USE_PARAMETER_STRUCT(FRayTracingBuildInstanceBufferCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, GPUSceneInstanceSceneData)
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, GPUSceneInstancePayloadData)
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, GPUScenePrimitiveSceneData)

		SHADER_PARAMETER_UAV(RWStructuredBuffer, InstancesDescriptors)
		SHADER_PARAMETER_SRV(StructuredBuffer<FRayTracingInstanceDescriptorInput>, InputInstanceDescriptors)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, AccelerationStructureAddresses)
		SHADER_PARAMETER_SRV(StructuredBuffer, InstanceTransforms)

		SHADER_PARAMETER(FVector3f, FarFieldReferencePos)

		SHADER_PARAMETER(uint32, NumInstances)
		SHADER_PARAMETER(uint32, InputDescOffset)

		SHADER_PARAMETER(uint32, InstanceSceneDataSOAStride)

		SHADER_PARAMETER(FVector3f, PreViewTranslationHigh)
		SHADER_PARAMETER(FVector3f, PreViewTranslationLow)

		// Instance culling params
		SHADER_PARAMETER(float, CullingRadius)
		SHADER_PARAMETER(float, FarFieldCullingRadius)
		SHADER_PARAMETER(float, AngleThresholdRatioSq)
		SHADER_PARAMETER(FVector3f, ViewOrigin)
		SHADER_PARAMETER(uint32, CullingMode)

		SHADER_PARAMETER_UAV(RWStructuredBuffer<uint>, RWOutputStats)

		// Debug parameters
		SHADER_PARAMETER_UAV(RWStructuredBuffer<uint>, RWDebugInstanceGPUSceneIndices)
	END_SHADER_PARAMETER_STRUCT()

	class FUseGPUSceneDim : SHADER_PERMUTATION_BOOL("USE_GPUSCENE");
	class FOutputInstanceGPUSceneIndexDim : SHADER_PERMUTATION_BOOL("OUTPUT_INSTANCE_GPUSCENE_INDEX");
	class FGpuCullingDim : SHADER_PERMUTATION_BOOL("GPU_CULLING");
	class FOutputStatsDim : SHADER_PERMUTATION_BOOL("OUTPUT_STATS");
	class FUseWaveOpsDim : SHADER_PERMUTATION_BOOL("USE_WAVE_OPS");
	using FPermutationDomain = TShaderPermutationDomain<FUseGPUSceneDim, FOutputInstanceGPUSceneIndexDim, FGpuCullingDim, FOutputStatsDim, FUseWaveOpsDim>;
		
	static constexpr uint32 ThreadGroupSize = 64;

	static inline void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), ThreadGroupSize);
		OutEnvironment.SetDefine(TEXT("VF_SUPPORTS_PRIMITIVE_SCENE_DATA"), 1);
		OutEnvironment.SetDefine(TEXT("USE_GLOBAL_GPU_SCENE_DATA"), 1);

		// Force DXC to avoid shader reflection issues.
		OutEnvironment.CompilerFlags.Add(CFLAG_ForceDXC);
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		if (PermutationVector.Get<FUseWaveOpsDim>() && !RHISupportsWaveOperations(Parameters.Platform))
		{
			return false;
		}

		return IsRayTracingEnabledForProject(Parameters.Platform);
	}
};

IMPLEMENT_GLOBAL_SHADER(FRayTracingBuildInstanceBufferCS, "/Engine/Private/Raytracing/RayTracingInstanceBufferUtil.usf", "RayTracingBuildInstanceBufferCS", SF_Compute);

void BuildRayTracingInstanceBuffer(
	FRHICommandList& RHICmdList,
	const FGPUScene* GPUScene,
	const FDFVector3& PreViewTranslation,
	uint32 NumInstances,
	uint32 InputDescOffset,
	FUnorderedAccessViewRHIRef InstancesUAV,
	FShaderResourceViewRHIRef InstanceUploadSRV,
	FShaderResourceViewRHIRef AccelerationStructureAddressesSRV,
	FShaderResourceViewRHIRef InstanceTransformSRV,
	const FRayTracingCullingParameters* CullingParameters,
	FUnorderedAccessViewRHIRef OutputStatsUAV,
	FUnorderedAccessViewRHIRef DebugInstanceGPUSceneIndexUAV)
{
	FRayTracingBuildInstanceBufferCS::FParameters PassParams;
	PassParams.InstancesDescriptors = InstancesUAV;
	PassParams.InputInstanceDescriptors = InstanceUploadSRV;
	PassParams.AccelerationStructureAddresses = AccelerationStructureAddressesSRV;
	PassParams.InstanceTransforms = InstanceTransformSRV;
	PassParams.FarFieldReferencePos = (FVector3f)Lumen::GetFarFieldReferencePos();	// LWC_TODO: Precision Loss
	PassParams.NumInstances = NumInstances;
	PassParams.InputDescOffset = InputDescOffset;
	PassParams.PreViewTranslationHigh = PreViewTranslation.High;
	PassParams.PreViewTranslationLow = PreViewTranslation.Low;

	if (GPUScene)
	{
		PassParams.InstanceSceneDataSOAStride = GPUScene->InstanceSceneDataSOAStride;
		PassParams.GPUSceneInstanceSceneData = GPUScene->InstanceSceneDataBuffer->GetSRV();
		PassParams.GPUSceneInstancePayloadData = GPUScene->InstancePayloadDataBuffer->GetSRV();
		PassParams.GPUScenePrimitiveSceneData = GPUScene->PrimitiveBuffer->GetSRV();
	}

	if (CullingParameters)
	{
		PassParams.CullingRadius = CullingParameters->CullingRadius;
		PassParams.FarFieldCullingRadius = CullingParameters->FarFieldCullingRadius;
		PassParams.AngleThresholdRatioSq = CullingParameters->AngleThresholdRatioSq;
		PassParams.ViewOrigin = CullingParameters->TranslatedViewOrigin;
		PassParams.CullingMode = uint32(CullingParameters->CullingMode);
	}

	PassParams.RWOutputStats = OutputStatsUAV;

	PassParams.RWDebugInstanceGPUSceneIndices = DebugInstanceGPUSceneIndexUAV;

	FRayTracingBuildInstanceBufferCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FRayTracingBuildInstanceBufferCS::FUseGPUSceneDim>(InstanceTransformSRV == nullptr);
	PermutationVector.Set<FRayTracingBuildInstanceBufferCS::FOutputInstanceGPUSceneIndexDim>(DebugInstanceGPUSceneIndexUAV != nullptr);
	PermutationVector.Set<FRayTracingBuildInstanceBufferCS::FOutputStatsDim>(OutputStatsUAV != nullptr);
	PermutationVector.Set<FRayTracingBuildInstanceBufferCS::FUseWaveOpsDim>(GRHISupportsWaveOperations);
	PermutationVector.Set<FRayTracingBuildInstanceBufferCS::FGpuCullingDim>(CullingParameters != nullptr);

	auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FRayTracingBuildInstanceBufferCS>(PermutationVector);
	const int32 GroupSize = FMath::DivideAndRoundUp(PassParams.NumInstances, FRayTracingBuildInstanceBufferCS::ThreadGroupSize);

	//ClearUnusedGraphResources(ComputeShader, &PassParams);

	SetComputePipelineState(RHICmdList, ComputeShader.GetComputeShader());

	SetShaderParameters(RHICmdList, ComputeShader, ComputeShader.GetComputeShader(), PassParams);

	DispatchComputeShader(RHICmdList, ComputeShader.GetShader(), GroupSize, 1, 1);

	UnsetShaderUAVs(RHICmdList, ComputeShader, ComputeShader.GetComputeShader());
}

void BuildRayTracingInstanceBuffer(
	FRHICommandList& RHICmdList,
	const FGPUScene* GPUScene,
	const FDFVector3& PreViewTranslation,
	FUnorderedAccessViewRHIRef InstancesUAV,
	FShaderResourceViewRHIRef InstanceUploadSRV,
	FShaderResourceViewRHIRef AccelerationStructureAddressesSRV,
	FShaderResourceViewRHIRef CPUInstanceTransformSRV,
	uint32 NumNativeGPUSceneInstances,
	uint32 NumNativeCPUInstances,
	const FRayTracingCullingParameters* CullingParameters,
	FUnorderedAccessViewRHIRef OutputStatsUAV,
	FUnorderedAccessViewRHIRef DebugInstanceGPUSceneIndexUAV)
{
	if (NumNativeGPUSceneInstances > 0)
	{
		BuildRayTracingInstanceBuffer(
			RHICmdList,
			GPUScene,
			PreViewTranslation,
			NumNativeGPUSceneInstances,
			0,
			InstancesUAV,
			InstanceUploadSRV,
			AccelerationStructureAddressesSRV,
			nullptr,
			CullingParameters,
			OutputStatsUAV,
			DebugInstanceGPUSceneIndexUAV);
	}

	if (NumNativeCPUInstances > 0)
	{
		BuildRayTracingInstanceBuffer(
			RHICmdList,
			GPUScene,
			PreViewTranslation,
			NumNativeCPUInstances,
			NumNativeGPUSceneInstances, // CPU instance input descriptors are stored after GPU Scene instances
			InstancesUAV,
			InstanceUploadSRV,
			AccelerationStructureAddressesSRV,
			CPUInstanceTransformSRV,
			nullptr,
			OutputStatsUAV,
			nullptr);
	}
}

#endif //RHI_RAYTRACING
