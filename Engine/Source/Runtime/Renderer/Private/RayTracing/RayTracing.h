// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RendererInterface.h"
#include "RenderGraphDefinitions.h"
#include "RayTracingDefinitions.h"
#include "RHIDefinitions.h"
#include "ShaderCore.h"

enum class EDiffuseIndirectMethod;
enum class EReflectionsMethod;
class FRayTracingScene;
class FRayTracingShaderBindingTable;
class FScene;
class FViewInfo;
class FViewFamilyInfo;
class FGlobalDynamicReadBuffer;

// Settings controlling ray tracing instance caching
namespace RayTracing
{
	struct FSceneOptions
	{
		bool bTranslucentGeometry = true;

		FSceneOptions(
			const FScene& Scene,
			const FViewFamilyInfo& ViewFamily,
			const FViewInfo& View,
			EDiffuseIndirectMethod DiffuseIndirectMethod,
			EReflectionsMethod ReflectionsMethod);
	};
};

#if RHI_RAYTRACING

namespace RayTracing
{
	struct FGatherInstancesTaskData;

	void OnRenderBegin(FScene& Scene, TArray<FViewInfo>& Views, const FViewFamilyInfo& ViewFamily);

	// Get shader resource table desc used for all raytracing shaders which is shared between all shaders in the RTPSO
	const FShaderBindingLayout* GetShaderBindingLayout(EShaderPlatform ShaderPlatform);

	// Setup the runtime static uniform buffer bindings on the command list if enabled
	TOptional<FScopedUniformBufferStaticBindings> BindStaticUniformBufferBindings(const FViewInfo& View, FRHIUniformBuffer* SceneUniformBuffer, FRHICommandList& RHICmdList);

	FGatherInstancesTaskData* CreateGatherInstancesTaskData(
		FSceneRenderingBulkObjectAllocator& InAllocator,
		FScene& Scene,
		FViewInfo& View,
		const FViewFamilyInfo& ViewFamily,
		EDiffuseIndirectMethod DiffuseIndirectMethod,
		EReflectionsMethod ReflectionsMethod);
	
	void BeginGatherInstances(FGatherInstancesTaskData& TaskData, UE::Tasks::FTask FrustumCullTask);

	// Fills RayTracingScene instance list for the given View and adds relevant ray tracing data to the view. Does not reset previous scene contents.
	// This function must run on render thread
	bool FinishGatherInstances(
		FRDGBuilder& GraphBuilder,
		FGatherInstancesTaskData& TaskData,
		FRayTracingScene& RayTracingScene,
		FRayTracingShaderBindingTable& RayTracingSBT,
		FGlobalDynamicReadBuffer& InDynamicReadBuffer,
		FSceneRenderingBulkObjectAllocator& InBulkAllocator);

	bool ShouldExcludeDecals();
}

#endif // RHI_RAYTRACING