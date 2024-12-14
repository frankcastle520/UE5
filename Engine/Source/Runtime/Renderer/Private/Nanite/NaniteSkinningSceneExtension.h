// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SpanAllocator.h"
#include "Containers/Map.h"
#include "SceneExtensions.h"
#include "Skinning/SkinningTransformProvider.h"
#include "NaniteDefinitions.h"
#include "RendererPrivateUtils.h"
#include "Matrix3x4.h"
#include "Delegates/DelegateCombinations.h"
#include "Delegates/Delegate.h"
#include "Tickable.h"

class FNaniteSkinningParameters;

namespace Nanite
{

class FSkinnedSceneProxy;
class FSkinningSceneExtension : public ISceneExtension, public FTickableGameObject
{
	DECLARE_SCENE_EXTENSION(RENDERER_API, FSkinningSceneExtension);

public:
	class FUpdater : public ISceneExtensionUpdater
	{
		DECLARE_SCENE_EXTENSION_UPDATER(FUpdater, FSkinningSceneExtension);

	public:
		FUpdater(FSkinningSceneExtension& InSceneData);

		virtual void End();
		virtual void PreSceneUpdate(FRDGBuilder& GraphBuilder, const FScenePreUpdateChangeSet& ChangeSet, FSceneUniformBuffer& SceneUniforms) override;
		virtual void PostSceneUpdate(FRDGBuilder& GraphBuilder, const FScenePostUpdateChangeSet& ChangeSet) override;
		
		void PostMeshUpdate(FRDGBuilder& GraphBuilder, const TConstArrayView<FPrimitiveSceneInfo*>& SceneInfosWithStaticDrawListUpdate);

	private:
		FSkinningSceneExtension* SceneData = nullptr;
		TConstArrayView<FPrimitiveSceneInfo*> AddedList;
		TConstArrayView<FPrimitiveSceneInfo*> UpdateList;
		TArray<int32, FSceneRenderingArrayAllocator> DirtyPrimitiveList;
		const bool bEnableAsync = true;
		bool bForceFullUpload = false;
		bool bDefragging = false;
	};

	class FRenderer : public ISceneExtensionRenderer
	{
		DECLARE_SCENE_EXTENSION_RENDERER(FRenderer, FSkinningSceneExtension);
	
	public:
		FRenderer(FSkinningSceneExtension& InSceneData) : SceneData(&InSceneData) {}
		virtual void UpdateSceneUniformBuffer(FRDGBuilder& GraphBuilder, FSceneUniformBuffer& Buffer) override;

	private:
		FSkinningSceneExtension* SceneData = nullptr;
	};

	friend class FUpdater;

	static bool ShouldCreateExtension(FScene& InScene);

	virtual void InitExtension(FScene& InScene) override;

	virtual ISceneExtensionUpdater* CreateUpdater() override;
	virtual ISceneExtensionRenderer* CreateRenderer() override;

	RENDERER_API void GetSkinnedPrimitives(TArray<FPrimitiveSceneInfo*>& OutPrimitives) const;

	RENDERER_API static const FSkinningTransformProvider::FProviderId& GetRefPoseProviderId();
	RENDERER_API static const FSkinningTransformProvider::FProviderId& GetAnimRuntimeProviderId();

private:
	enum ETask : uint32
	{
		FreeBufferSpaceTask,
		InitHeaderDataTask,
		AllocBufferSpaceTask,
		UploadHeaderDataTask,
		UploadHierarchyDataTask,
		UploadTransformDataTask,

		NumTasks
	};

	struct FHeaderData
	{
		FPrimitiveSceneInfo* PrimitiveSceneInfo	= nullptr;
		uint32 ObjectSpaceBufferOffset			= INDEX_NONE;
		uint32 ObjectSpaceBufferCount			= 0;
		uint32 HierarchyBufferOffset			= INDEX_NONE;
		uint32 HierarchyBufferCount				= 0;
		uint32 TransformBufferOffset			= INDEX_NONE;
		uint32 TransformBufferCount				= 0;
		uint16 MaxTransformCount				= 0;
		uint8  MaxInfluenceCount				= 0;
		uint8  UniqueAnimationCount				= 1;
		uint8  bHasScale : 1					= false;

		FNaniteSkinningHeader Pack() const
		{
			// Verify that the buffer offsets all fit within the encoded range prior to packing
			check(
				HierarchyBufferOffset	<= SKINNING_BUFFER_OFFSET_MAX &&
				TransformBufferOffset	<= SKINNING_BUFFER_OFFSET_MAX &&
				ObjectSpaceBufferOffset	<= SKINNING_BUFFER_OFFSET_MAX
			);

			FNaniteSkinningHeader Output;
			Output.HierarchyBufferOffset	= HierarchyBufferOffset;
			Output.TransformBufferOffset	= TransformBufferOffset;
			Output.ObjectSpaceBufferOffset	= ObjectSpaceBufferOffset;
			Output.MaxTransformCount		= MaxTransformCount;
			Output.MaxInfluenceCount		= MaxInfluenceCount;
			Output.UniqueAnimationCount		= UniqueAnimationCount;
			Output.bHasScale				= bHasScale;
			Output.Padding					= 0;
			return Output;
		}
	};

	class FBuffers
	{
	public:
		FBuffers();

		TPersistentByteAddressBuffer<FNaniteSkinningHeader> HeaderDataBuffer;
		TPersistentByteAddressBuffer<uint32> BoneHierarchyBuffer;
		TPersistentByteAddressBuffer<float> BoneObjectSpaceBuffer;
		TPersistentByteAddressBuffer<FMatrix3x4> TransformDataBuffer;
	};
	
	class FUploader
	{
	public:
		TByteAddressBufferScatterUploader<FNaniteSkinningHeader> HeaderDataUploader;
		TByteAddressBufferScatterUploader<uint32> BoneHierarchyUploader;
		TByteAddressBufferScatterUploader<float> BoneObjectSpaceUploader;
		TByteAddressBufferScatterUploader<FMatrix3x4> TransformDataUploader;
	};
	
	bool IsEnabled() const { return Buffers.IsValid(); }
	void SetEnabled(bool bEnabled);
	void SyncAllTasks() const { UE::Tasks::Wait(TaskHandles); }

	void FinishSkinningBufferUpload(
		FRDGBuilder& GraphBuilder,
		FNaniteSkinningParameters* OutParams = nullptr
	);

	void PerformSkinning(
		FNaniteSkinningParameters& Parameters,
		FRDGBuilder& GraphBuilder
	);

	bool ProcessBufferDefragmentation();

	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual UWorld* GetTickableGameObjectWorld() const override;
	virtual bool IsTickable() const override { return true; }
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual bool IsTickableInEditor() const override { return true; }

private:
	FScene* Scene = nullptr;
	FSpanAllocator ObjectSpaceAllocator;
	FSpanAllocator HierarchyAllocator;
	FSpanAllocator TransformAllocator;
	TSparseArray<FHeaderData> HeaderData;
	TUniquePtr<FBuffers> Buffers;
	TUniquePtr<FUploader> Uploader;
	TStaticArray<UE::Tasks::FTask, NumTasks> TaskHandles;

	float DeltaTime = 0.0f;
	FVector CameraLocation;

public:
	RENDERER_API static void ProvideRefPoseTransforms(FSkinningTransformProvider::FProviderContext& Context);
	RENDERER_API static void ProvideAnimRuntimeTransforms(FSkinningTransformProvider::FProviderContext& Context);
};

} // namespace Nanite
