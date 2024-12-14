// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MeshPassProcessor.cpp: 
=============================================================================*/

#include "RayTracingMeshDrawCommands.h"
#include "SceneUniformBuffer.h"
#include "Nanite/NaniteShared.h"
#include "RayTracingDefinitions.h"
#include "RayTracingShaderBindingTable.h"

#if RHI_RAYTRACING

void FDynamicRayTracingMeshCommandContext::FinalizeCommand(FRayTracingMeshCommand& RayTracingMeshCommand)
{
	check(GeometrySegmentIndex == RayTracingMeshCommand.GeometrySegmentIndex);

	if (SBTAllocation)
	{
		if (SBTAllocation->HasLayer(ERayTracingSceneLayer::Base))
		{
			const bool bHidden = RayTracingMeshCommand.bDecal;
			const uint32 RecordIndex = SBTAllocation->GetRecordIndex(ERayTracingSceneLayer::Base, RayTracingMeshCommand.GeometrySegmentIndex);
			FRayTracingShaderBindingData DirtyShaderBinding(&RayTracingMeshCommand, RayTracingGeometry, RecordIndex, bHidden);
			DirtyShaderBindings.Add(DirtyShaderBinding);
		}

		if (SBTAllocation->HasLayer(ERayTracingSceneLayer::Decals))
		{
			const bool bHidden = !RayTracingMeshCommand.bDecal;
			const uint32 RecordIndex = SBTAllocation->GetRecordIndex(ERayTracingSceneLayer::Decals, RayTracingMeshCommand.GeometrySegmentIndex);
			FRayTracingShaderBindingData DirtyShaderBinding(&RayTracingMeshCommand, RayTracingGeometry, RecordIndex, bHidden);
			DirtyShaderBindings.Add(DirtyShaderBinding);
		}
	}

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
		if (RayTracingInstanceIndex != INDEX_NONE)
		{
			const bool bHidden = RayTracingMeshCommand.bDecal;
			FRayTracingShaderBindingData DirtyShaderBinding(&RayTracingMeshCommand, RayTracingInstanceIndex, bHidden);
			DirtyShaderBindings.Add(DirtyShaderBinding);
		}

	if (RayTracingDecalInstanceIndex != INDEX_NONE)
	{
		const bool bHidden = !RayTracingMeshCommand.bDecal;
		FRayTracingShaderBindingData DirtyShaderBinding(&RayTracingMeshCommand, RayTracingDecalInstanceIndex, bHidden);
		DirtyShaderBindings.Add(DirtyShaderBinding);
	}
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

void FRayTracingMeshCommand::SetRayTracingShaderBindingsForHitGroup(
	FRayTracingLocalShaderBindingWriter* BindingWriter,
	const TUniformBufferRef<FViewUniformShaderParameters>& ViewUniformBuffer,
	FRHIUniformBuffer* SceneUniformBuffer,
	FRHIUniformBuffer* NaniteUniformBuffer,
	uint32 RecordIndex,
	const FRHIRayTracingGeometry* RayTracingGeometry,
	uint32 SegmentIndex,
	uint32 HitGroupIndexInPipeline) const
{
	FRayTracingLocalShaderBindings* Bindings = ShaderBindings.SetRayTracingShaderBindingsForHitGroup(BindingWriter, RecordIndex, RayTracingGeometry, SegmentIndex, HitGroupIndexInPipeline);

	if (ViewUniformBufferParameter.IsBound())
	{
		check(ViewUniformBuffer);
		Bindings->UniformBuffers[ViewUniformBufferParameter.GetBaseIndex()] = ViewUniformBuffer;
	}

	if (SceneUniformBufferParameter.IsBound())
	{
		check(SceneUniformBuffer);
		Bindings->UniformBuffers[SceneUniformBufferParameter.GetBaseIndex()] = SceneUniformBuffer;
	}

	if (NaniteUniformBufferParameter.IsBound())
	{
		check(NaniteUniformBuffer);
		Bindings->UniformBuffers[NaniteUniformBufferParameter.GetBaseIndex()] = NaniteUniformBuffer;
	}
}

void FRayTracingMeshCommand::SetRayTracingShaderBindingsForHitGroup(
	FRayTracingLocalShaderBindingWriter* BindingWriter,
	const TUniformBufferRef<FViewUniformShaderParameters>& ViewUniformBuffer,
	FRHIUniformBuffer* SceneUniformBuffer,
	FRHIUniformBuffer* NaniteUniformBuffer,
	uint32 InstanceIndex,
	uint32 SegmentIndex,
	uint32 HitGroupIndexInPipeline,
	uint32 ShaderSlot) const
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	FRayTracingLocalShaderBindings* Bindings = ShaderBindings.SetRayTracingShaderBindingsForHitGroup(BindingWriter, InstanceIndex, SegmentIndex, HitGroupIndexInPipeline, ShaderSlot);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	if (ViewUniformBufferParameter.IsBound())
	{
		check(ViewUniformBuffer);
		Bindings->UniformBuffers[ViewUniformBufferParameter.GetBaseIndex()] = ViewUniformBuffer;
	}

	if (SceneUniformBufferParameter.IsBound())
	{
		check(SceneUniformBuffer);
		Bindings->UniformBuffers[SceneUniformBufferParameter.GetBaseIndex()] = SceneUniformBuffer;
	}

	if (NaniteUniformBufferParameter.IsBound())
	{
		check(NaniteUniformBuffer);
		Bindings->UniformBuffers[NaniteUniformBufferParameter.GetBaseIndex()] = NaniteUniformBuffer;
	}
}

void FRayTracingMeshCommand::SetShader(const TShaderRef<FShader>& Shader)
{
	check(Shader.IsValid());
	MaterialShaderIndex = Shader.GetRayTracingHitGroupLibraryIndex();
	MaterialShader = Shader.GetRayTracingShader();
	ViewUniformBufferParameter = Shader->GetUniformBufferParameter<FViewUniformShaderParameters>();
	SceneUniformBufferParameter = Shader->GetUniformBufferParameter<FSceneUniformParameters>();
	NaniteUniformBufferParameter = Shader->GetUniformBufferParameter<FNaniteRayTracingUniformParameters>();
	ShaderBindings.Initialize(Shader);
}

void FRayTracingMeshCommand::SetShaders(const FMeshProcessorShaders& Shaders)
{
	SetShader(Shaders.RayTracingShader);
}

bool FRayTracingMeshCommand::IsUsingNaniteRayTracing() const
{
	return NaniteUniformBufferParameter.IsBound();
}

void FRayTracingMeshCommand::UpdateFlags(FRayTracingCachedMeshCommandFlags& Flags) const
{
	Flags.InstanceMask |= InstanceMask;
	Flags.bAllSegmentsOpaque &= bOpaque;
	Flags.bAllSegmentsCastShadow &= bCastRayTracedShadows;
	Flags.bAnySegmentsCastShadow |= bCastRayTracedShadows;
	Flags.bAnySegmentsDecal |= bDecal;
	Flags.bAllSegmentsDecal &= bDecal;
	Flags.bTwoSided |= bTwoSided;
	Flags.bIsSky |= bIsSky;
	Flags.bAllSegmentsTranslucent &= bIsTranslucent;
	Flags.bAllSegmentsReverseCulling &= bReverseCulling;
}

void FRayTracingShaderCommand::SetRayTracingShaderBindings(
	FRayTracingLocalShaderBindingWriter* BindingWriter,
	const TUniformBufferRef<FViewUniformShaderParameters>& ViewUniformBuffer,
	FRHIUniformBuffer* SceneUniformBuffer,
	FRHIUniformBuffer* NaniteUniformBuffer,
	uint32 ShaderIndexInPipeline,
	uint32 ShaderSlot) const
{
	FRayTracingLocalShaderBindings* Bindings = ShaderBindings.SetRayTracingShaderBindings(BindingWriter, ShaderIndexInPipeline, ShaderSlot);

	if (ViewUniformBufferParameter.IsBound())
	{
		check(ViewUniformBuffer);
		Bindings->UniformBuffers[ViewUniformBufferParameter.GetBaseIndex()] = ViewUniformBuffer;
	}

	if (SceneUniformBufferParameter.IsBound())
	{
		check(SceneUniformBuffer);
		Bindings->UniformBuffers[SceneUniformBufferParameter.GetBaseIndex()] = SceneUniformBuffer;
	}

	if (NaniteUniformBufferParameter.IsBound())
	{
		check(NaniteUniformBuffer);
		Bindings->UniformBuffers[NaniteUniformBufferParameter.GetBaseIndex()] = NaniteUniformBuffer;
	}
}

void FRayTracingShaderCommand::SetShader(const TShaderRef<FShader>& InShader)
{
	check(InShader->GetFrequency() == SF_RayCallable || InShader->GetFrequency() == SF_RayMiss);
	ShaderIndex = InShader.GetRayTracingCallableShaderLibraryIndex();
	Shader = InShader.GetRayTracingShader();
	ViewUniformBufferParameter = InShader->GetUniformBufferParameter<FViewUniformShaderParameters>();
	SceneUniformBufferParameter = InShader->GetUniformBufferParameter<FSceneUniformParameters>();
	NaniteUniformBufferParameter = InShader->GetUniformBufferParameter<FNaniteRayTracingUniformParameters>();

	ShaderBindings.Initialize(InShader);
}
#endif // RHI_RAYTRACING