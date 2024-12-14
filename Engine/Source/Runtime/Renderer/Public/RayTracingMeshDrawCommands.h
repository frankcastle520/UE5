// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MeshPassProcessor.h
=============================================================================*/

#pragma once

#include "MeshPassProcessor.h"

class FRayTracingGeometry;
struct FRayTracingSBTAllocation;

struct FRayTracingCachedMeshCommandFlags
{
	FRayTracingCachedMeshCommandFlags()
	{	
		Data = 0;

		bAllSegmentsOpaque = true;
		bAllSegmentsCastShadow = true;
		bAnySegmentsCastShadow = false;
		bAnySegmentsDecal = false;
		bAllSegmentsDecal = true;
		bTwoSided = false;
		bIsSky = false;
		bAllSegmentsTranslucent = true;
		bAllSegmentsReverseCulling = true;
	}

	bool operator==(const FRayTracingCachedMeshCommandFlags& Other) const
	{
		return CachedMeshCommandHash == Other.CachedMeshCommandHash &&
			Data == Other.Data;
	}

	bool operator!=(const FRayTracingCachedMeshCommandFlags& Other) const
	{
		return !(*this == Other);
	}

	friend uint32 GetTypeHash(const FRayTracingCachedMeshCommandFlags& Key)
	{
		return HashCombine(GetTypeHash(Key.CachedMeshCommandHash), Key.Data);
	}

	uint64 CachedMeshCommandHash = 0;
	union
	{
		struct
		{
			uint8 InstanceMask;
			bool bAllSegmentsOpaque : 1;
			bool bAllSegmentsCastShadow : 1;
			bool bAnySegmentsCastShadow : 1;
			bool bAnySegmentsDecal : 1;
			bool bAllSegmentsDecal : 1;
			bool bTwoSided : 1;
			bool bIsSky : 1;
			bool bAllSegmentsTranslucent : 1;
			bool bAllSegmentsReverseCulling : 1;
		};
		uint32 Data;
	};
};

class FRayTracingMeshCommand
{
public:
	FMeshDrawShaderBindings ShaderBindings;
	FRHIRayTracingShader* MaterialShader = nullptr;

	uint32 MaterialShaderIndex = UINT_MAX;
	uint32 GeometrySegmentIndex = UINT_MAX;
	uint8 InstanceMask = 0xFF;

	bool bCastRayTracedShadows = true;
	bool bOpaque = true;
	bool bAlphaMasked = false;
	bool bDecal = false;
	bool bIsSky = false;
	bool bIsTranslucent = false;
	bool bTwoSided = false;
	bool bReverseCulling = false;

	RENDERER_API void SetRayTracingShaderBindingsForHitGroup(
		FRayTracingLocalShaderBindingWriter* BindingWriter,
		const TUniformBufferRef<FViewUniformShaderParameters>& ViewUniformBuffer,
		FRHIUniformBuffer* SceneUniformBuffer,
		FRHIUniformBuffer* NaniteUniformBuffer,
		uint32 RecordIndex,
		const FRHIRayTracingGeometry* RayTracingGeometry,
		uint32 SegmentIndex,
		uint32 HitGroupIndexInPipeline) const;

	UE_DEPRECATED(5.5, "Provide RayTracingGeometry and SBTSegmentIndex instead of InstanceIndex")
	RENDERER_API void SetRayTracingShaderBindingsForHitGroup(
		FRayTracingLocalShaderBindingWriter* BindingWriter,
		const TUniformBufferRef<FViewUniformShaderParameters>& ViewUniformBuffer,
		FRHIUniformBuffer* SceneUniformBuffer,
		FRHIUniformBuffer* NaniteUniformBuffer,
		uint32 InstanceIndex,
		uint32 SegmentIndex,
		uint32 HitGroupIndexInPipeline,
		uint32 ShaderSlot) const;

	/** Sets ray hit group shaders on the mesh command and allocates room for the shader bindings. */
	RENDERER_API void SetShader(const TShaderRef<FShader>& Shader);

	UE_DEPRECATED(5.4, "Use SetShader")
	RENDERER_API void SetShaders(const FMeshProcessorShaders& Shaders);

	RENDERER_API bool IsUsingNaniteRayTracing() const;

	RENDERER_API void UpdateFlags(FRayTracingCachedMeshCommandFlags& Flags) const;

private:
	FShaderUniformBufferParameter ViewUniformBufferParameter;
	FShaderUniformBufferParameter SceneUniformBufferParameter;
	FShaderUniformBufferParameter NaniteUniformBufferParameter;
};


class FRayTracingShaderBindingData
{
public:
	FRayTracingShaderBindingData(const FRayTracingMeshCommand* InRayTracingMeshCommand, const FRHIRayTracingGeometry* InRayTracingGeometry, uint32 InSBTRecordIndex, bool bInHidden)
		: RayTracingMeshCommand(InRayTracingMeshCommand)
		, RayTracingGeometry(InRayTracingGeometry)
		, SBTRecordIndex(InSBTRecordIndex)
		, InstanceIndex(INDEX_NONE)
		, bHidden(bInHidden)
	{
		check(RayTracingGeometry != nullptr);
	}

	UE_DEPRECATED(5.5, "Provide RayTracingGeometry and SBTSegmentIndex instead of InstanceIndex")
	FRayTracingShaderBindingData(const FRayTracingMeshCommand* InRayTracingMeshCommand, uint32 InInstanceIndex, bool bInHidden = false)
		: RayTracingMeshCommand(InRayTracingMeshCommand)
		, RayTracingGeometry(nullptr)
		, SBTRecordIndex(INDEX_NONE)
		, InstanceIndex(InInstanceIndex)
		, bHidden(bInHidden)
	{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		check(InstanceIndex != INDEX_NONE);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}
	
	const FRayTracingMeshCommand* RayTracingMeshCommand;
	const FRHIRayTracingGeometry* RayTracingGeometry;
	uint32 SBTRecordIndex;
	UE_DEPRECATED(5.5, "Provide RayTracingGeometry and SBTSegmentIndex instead of InstanceIndex")
	uint32 InstanceIndex;
	bool bHidden;
	
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	FRayTracingShaderBindingData(const FRayTracingShaderBindingData&) = default;
	FRayTracingShaderBindingData& operator=(const FRayTracingShaderBindingData&) = default;
	FRayTracingShaderBindingData(FRayTracingShaderBindingData&&) = default;
	FRayTracingShaderBindingData& operator=(FRayTracingShaderBindingData&&) = default;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
};

template <>
struct TUseBitwiseSwap<FRayTracingShaderBindingData>
{
	// Prevent Memcpy call overhead during FRayTracingShaderBindingData sorting
	enum { Value = false };
};

typedef TArray<FRayTracingShaderBindingData> FRayTracingShaderBindingDataOneFrameArray;

UE_DEPRECATED(5.5, "Use FRayTracingShaderBindingData instead of FVisibleRayTracingMeshCommand");
using FVisibleRayTracingMeshCommand = FRayTracingShaderBindingData;

UE_DEPRECATED(5.5, "Use FRayTracingShaderBindingDataOneFrameArray instead of FRayTracingMeshCommandOneFrameArray");
using FRayTracingMeshCommandOneFrameArray = FRayTracingShaderBindingDataOneFrameArray;

class FRayTracingMeshCommandContext
{
public:

	virtual ~FRayTracingMeshCommandContext() {}

	virtual FRayTracingMeshCommand& AddCommand(const FRayTracingMeshCommand& Initializer) = 0;

	virtual void FinalizeCommand(FRayTracingMeshCommand& RayTracingMeshCommand) = 0;
};

using FTempRayTracingMeshCommandStorage = TArray<FRayTracingMeshCommand>;

using FCachedRayTracingMeshCommandStorage = TSparseArray<FRayTracingMeshCommand>;

using FDynamicRayTracingMeshCommandStorage = TChunkedArray<FRayTracingMeshCommand>;

template<class T>
class FCachedRayTracingMeshCommandContext : public FRayTracingMeshCommandContext
{
public:
	FCachedRayTracingMeshCommandContext(T& InDrawListStorage) : DrawListStorage(InDrawListStorage) {}

	virtual FRayTracingMeshCommand& AddCommand(const FRayTracingMeshCommand& Initializer) override final
	{
		CommandIndex = DrawListStorage.Add(Initializer);
		return DrawListStorage[CommandIndex];
	}

	virtual void FinalizeCommand(FRayTracingMeshCommand& RayTracingMeshCommand) override final {}

	int32 CommandIndex = -1;

private:
	T& DrawListStorage;
};

class FDynamicRayTracingMeshCommandContext : public FRayTracingMeshCommandContext
{
public:
	FDynamicRayTracingMeshCommandContext
	(
		FDynamicRayTracingMeshCommandStorage& InDynamicCommandStorage,
		FRayTracingShaderBindingDataOneFrameArray& InDirtyShaderBindings,
		const FRHIRayTracingGeometry* InRayTracingGeometry,
		uint32 InGeometrySegmentIndex,
		FRayTracingSBTAllocation* InSBTAllocation
	) :
		DynamicCommandStorage(InDynamicCommandStorage),
		DirtyShaderBindings(InDirtyShaderBindings),
		RayTracingGeometry(InRayTracingGeometry),
		GeometrySegmentIndex(InGeometrySegmentIndex),
		SBTAllocation(InSBTAllocation),
		RayTracingInstanceIndex(INDEX_NONE),
		RayTracingDecalInstanceIndex(INDEX_NONE)
	{}

	UE_DEPRECATED(5.5, "Provide RayTracingGeometry and GlobalGeometrySegmentIndex and DecalGlobalGeometrySegmentIndex instead")
	FDynamicRayTracingMeshCommandContext
	(
		FDynamicRayTracingMeshCommandStorage& InDynamicCommandStorage,
		FRayTracingShaderBindingDataOneFrameArray& InDirtyShaderBindings,
		uint32 InGeometrySegmentIndex,
		uint32 InRayTracingInstanceIndex,
		uint32 InRayTracingDecalInstanceIndex = INDEX_NONE
	) :
		DynamicCommandStorage(InDynamicCommandStorage),
		DirtyShaderBindings(InDirtyShaderBindings),
		RayTracingGeometry(nullptr),
		GeometrySegmentIndex(InGeometrySegmentIndex),
		SBTAllocation(nullptr),
		RayTracingInstanceIndex(InRayTracingInstanceIndex),
		RayTracingDecalInstanceIndex(InRayTracingDecalInstanceIndex)
	{}

	virtual FRayTracingMeshCommand& AddCommand(const FRayTracingMeshCommand& Initializer) override final
	{
		const int32 Index = DynamicCommandStorage.AddElement(Initializer);
		FRayTracingMeshCommand& NewCommand = DynamicCommandStorage[Index];
		NewCommand.GeometrySegmentIndex = GeometrySegmentIndex;
		return NewCommand;
	}

	RENDERER_API virtual void FinalizeCommand(FRayTracingMeshCommand& RayTracingMeshCommand) override final;

private:
	FDynamicRayTracingMeshCommandStorage& DynamicCommandStorage;
	FRayTracingShaderBindingDataOneFrameArray& DirtyShaderBindings;

	const FRHIRayTracingGeometry* RayTracingGeometry;
	uint32 GeometrySegmentIndex;

	FRayTracingSBTAllocation* SBTAllocation;

	UE_DEPRECATED(5.5, "Provide RayTracingGeometry and SBTSegmentRange instead of RayTracingInstanceIndex")
	uint32 RayTracingInstanceIndex;
	UE_DEPRECATED(5.5, "Provide RayTracingGeometry and DecalGlobalGeometrySegmentIndex instead of RayTracingDecalInstanceIndex")
	uint32 RayTracingDecalInstanceIndex;
};

class FRayTracingShaderCommand
{
public:
	FMeshDrawShaderBindings ShaderBindings;
	FRHIRayTracingShader* Shader = nullptr;

	uint32 ShaderIndex = UINT_MAX;
	uint32 SlotInScene = UINT_MAX;

	RENDERER_API void SetRayTracingShaderBindings(
		FRayTracingLocalShaderBindingWriter* BindingWriter,
		const TUniformBufferRef<FViewUniformShaderParameters>& ViewUniformBuffer,
		FRHIUniformBuffer* SceneUniformBuffer,
		FRHIUniformBuffer* NaniteUniformBuffer,
		uint32 ShaderIndexInPipeline,
		uint32 ShaderSlot) const;

	/** Sets ray tracing shader on the command and allocates room for the shader bindings. */
	RENDERER_API void SetShader(const TShaderRef<FShader>& Shader);

private:
	FShaderUniformBufferParameter ViewUniformBufferParameter;
	FShaderUniformBufferParameter SceneUniformBufferParameter;
	FShaderUniformBufferParameter NaniteUniformBufferParameter;
};
