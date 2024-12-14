// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InterchangePipelineBase.h"
#include "InterchangeSourceData.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "InterchangeGenericScenesPipeline.generated.h"

class ALevelInstance;
class UInterchangeActorFactoryNode;
class UInterchangeLevelFactoryNode;
class UInterchangeLevelInstanceActorFactoryNode;
class UInterchangeSceneNode;
class UInterchangeSceneVariantSetsNode;
class UInterchangeSceneImportAssetFactoryNode;
class UWorld;

UENUM(BlueprintType)
enum class EInterchangeSceneHierarchyType : uint8
{
	CreateLevelActors UMETA(DisplayName = "Create level actors", ToolTip = "Create actors in the current editor world for all scene nodes in the source hierarchy."),
	CreateLevelInstanceActor UMETA(DisplayName = "Create a level instance actor", ToolTip = "Create a world containing all scene nodes in the source hierarchy and reference the world has a level instance actor in the current editor world."),
};

UCLASS(BlueprintType, editinlinenew)
class INTERCHANGEPIPELINES_API UInterchangeGenericLevelPipeline : public UInterchangePipelineBase
{
	GENERATED_BODY()
public:
	/** The name of the pipeline that will be display in the import dialog. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scene", meta = (StandAlonePipelineProperty = "True", PipelineInternalEditionData = "True"))
	FString PipelineDisplayName;

	/* Set the reimport strategy when reimporting into the level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scene", meta = (SubCategory = "Actors properties", AdjustPipelineAndRefreshDetailOnChange = "True"))
	EReimportStrategyFlags ReimportPropertyStrategy = EReimportStrategyFlags::ApplyNoProperties;

	/* Choose how you want to import the hierarchy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scene")
	EInterchangeSceneHierarchyType SceneHierarchyType = EInterchangeSceneHierarchyType::CreateLevelActors;

	/* If enabled, deletes actors that were not part of the translation when reimporting into a level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scene", meta = (SubCategory = "Reimport Actors"))
	bool bDeleteMissingActors = false;

	/* If enabled, respawns actors that were deleted in the editor prior to a reimport. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scene", meta = (SubCategory = "Reimport Actors"))
	bool bForceReimportDeletedActors = false;

	/* If enabled, recreates assets that were deleted in the editor prior to reimporting into a level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scene", meta = (SubCategory = "Reimport Assets"))
	bool bForceReimportDeletedAssets = false;

	/* If enabled, deletes assets that were not part of the translation when reimporting into a level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scene", meta = (SubCategory = "Reimport Assets"))
	bool bDeleteMissingAssets = false;

	/** BEGIN UInterchangePipelineBase overrides */
	virtual void AdjustSettingsForContext(const FInterchangePipelineContextParams& ContextParams) override;

protected:

	virtual void ExecutePipeline(UInterchangeBaseNodeContainer* InBaseNodeContainer, const TArray<UInterchangeSourceData*>& InSourceDatas, const FString& ContentBasePath) override;
	virtual void ExecutePostImportPipeline(const UInterchangeBaseNodeContainer* BaseNodeContainer, const FString& NodeKey, UObject* CreatedAsset, bool bIsAReimport) override;
	virtual void ExecutePostBroadcastPipeline(const UInterchangeBaseNodeContainer* BaseNodeContainer, const FString& NodeKey, UObject* CreatedAsset, bool bIsAReimport) override;

	virtual bool CanExecuteOnAnyThread(EInterchangePipelineTask PipelineTask) override
	{
		return true;
	}
	/** END UInterchangePipelineBase overrides */

	/**
	 * PreImport step called for each translated SceneNode.
	 */
	virtual void ExecuteSceneNodePreImport(const FTransform& GlobalOffsetTransform, const UInterchangeSceneNode* SceneNode);

	/**
	 * PreImport step called for each translated SceneVariantSetNode.
	 */
	virtual void ExecuteSceneVariantSetNodePreImport(const UInterchangeSceneVariantSetsNode& SceneVariantSetNode);

	/**
	 * Return a new Actor Factory Node to be used for the given SceneNode.
	 */
	virtual UInterchangeActorFactoryNode* CreateActorFactoryNode(const UInterchangeSceneNode* SceneNode, const UInterchangeBaseNode* TranslatedAssetNode) const;

	/**
	 * Use to set up the given factory node's attributes after its initialization.
	 */
	virtual void SetUpFactoryNode(UInterchangeActorFactoryNode* ActorFactoryNode, const UInterchangeSceneNode* SceneNode, const UInterchangeBaseNode* TranslatedAssetNode) const;
	
	/** Disable this option to not convert Standard(Perspective) to Physical Cameras*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common Skeletal Meshes and Animations")
	bool bUsePhysicalInsteadOfStandardPerspectiveCamera = true;

protected:
	UInterchangeBaseNodeContainer* BaseNodeContainer = nullptr;
#if WITH_EDITORONLY_DATA
	/*
	 * Factory node created by the pipeline to later create the SceneImportAsset
	 * This factory node must be unique and depends on all the other factory nodes
	 * and its factory must be called after all factories
	 * Note that this factory node is not created at runtime. Therefore, the reimport
	 * of a level will not work at runtime
	 */
	UInterchangeSceneImportAssetFactoryNode* SceneImportFactoryNode = nullptr;
	UInterchangeLevelFactoryNode* LevelFactoryNode = nullptr;
	UInterchangeLevelInstanceActorFactoryNode* LevelInstanceActorFactoryNode = nullptr;

	struct FPostPipelineImportData
	{
		void AddWorld(UWorld* World);
		void AddLevelInstanceActor(ALevelInstance* LevelInstanceActor, UWorld* ReferenceWorld);
	private:
		bool UpdateLevelInstanceInternal(ALevelInstance* LevelInstanceActor, UWorld* ReferenceWorld);

		//The bool is to know if we have already update this world
		TMap<UWorld*, bool> Worlds;
		TMap<ALevelInstance*, UWorld*> ReferenceWorldPerLevelInstanceToUpdates;
	};

	FPostPipelineImportData PostPipelineImportData;
#endif

	void CacheActiveJointUids();
	TArray<FString> Cached_ActiveJointUids;
};


