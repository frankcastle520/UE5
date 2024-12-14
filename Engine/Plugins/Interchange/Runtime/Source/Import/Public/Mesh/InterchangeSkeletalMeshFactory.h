// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Animation/SkinWeightProfile.h"
#include "ClothingAsset.h"
#include "CoreMinimal.h"
#include "InterchangeFactoryBase.h"
#include "InterchangeMeshNode.h"
#include "Mesh/InterchangeMeshPayload.h"
#include "Rendering/SkeletalMeshLODImporterData.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "InterchangeSkeletalMeshFactory.generated.h"

class UInterchangeSceneNode;
class UInterchangeSkeletalMeshFactoryNode;
class USkeletalMesh;
class USkeleton;

namespace UE::Interchange
{
	//Get the mesh node context for each MeshUids
	struct FMeshNodeContext
	{
		const UInterchangeMeshNode* MeshNode = nullptr;
		const UInterchangeSceneNode* SceneNode = nullptr;
		TOptional<FTransform> SceneGlobalTransform;
		FInterchangeMeshPayLoadKey TranslatorPayloadKey;

		//Return a new key with the translator key merge with the transform
		FInterchangeMeshPayLoadKey GetTranslatorAndTransformPayloadKey() const;

		FInterchangeMeshPayLoadKey GetMorphTargetAndTransformPayloadKey(const FInterchangeMeshPayLoadKey& MorphTargetKey) const;

		//Return the translator key merge with the transform
		FString GetUniqueId() const;
	};
} //UE::Interchange

UCLASS(BlueprintType)
class INTERCHANGEIMPORT_API UInterchangeSkeletalMeshFactory : public UInterchangeFactoryBase
{
	GENERATED_BODY()
public:
	struct FImportAssetObjectLODData
	{
		int32 LodIndex = INDEX_NONE;
		TArray<FName> ExistingOriginalPerSectionMaterialImportName;
#if WITH_EDITOR
		TArray<SkeletalMeshImportData::FMaterial> ImportedMaterials;
		TArray<SkeletalMeshImportData::FBone> RefBonesBinary;
#endif
		TArray<UE::Interchange::FMeshNodeContext> MeshNodeContexts;
		bool bUseTimeZeroAsBindPose = false;
		bool bDiffPose = false;
	};

	struct FImportAssetObjectData
	{
		bool bIsReImport = false;
		USkeleton* SkeletonReference = nullptr;
		bool bApplyGeometryOnly = false;
		TArray<FImportAssetObjectLODData> LodDatas;

		TArray<FSkinWeightProfileInfo> ExistingSkinWeightProfileInfos;
		TArray<ClothingAssetUtils::FClothingAssetMeshBinding> ExistingClothingBindings;
#if WITH_EDITOR
		TArray<FSkeletalMeshImportData> ExistingAlternateImportDataPerLOD;
#endif

		bool IsValid() const;
	};
	//////////////////////////////////////////////////////////////////////////
	// Interchange factory base interface begin

	virtual UClass* GetFactoryClass() const override;
	virtual EInterchangeFactoryAssetType GetFactoryAssetType() override { return EInterchangeFactoryAssetType::Meshes; }
	virtual void CreatePayloadTasks(const FImportAssetObjectParams& Arguments, bool bAsync, TArray<TSharedPtr<UE::Interchange::FInterchangeTaskBase>>& PayloadTasks) override;
	virtual FImportAssetResult BeginImportAsset_GameThread(const FImportAssetObjectParams& Arguments) override;
	virtual FImportAssetResult ImportAsset_Async(const FImportAssetObjectParams& Arguments) override;
	virtual FImportAssetResult EndImportAsset_GameThread(const FImportAssetObjectParams& Arguments) override;
	virtual void Cancel() override;
	virtual void SetupObject_GameThread(const FSetupObjectParams& Arguments) override;
	virtual void BuildObject_GameThread(const FSetupObjectParams& Arguments, bool& OutPostEditchangeCalled) override;
	virtual void FinalizeObject_GameThread(const FSetupObjectParams& Arguments) override;
	virtual bool GetSourceFilenames(const UObject* Object, TArray<FString>& OutSourceFilenames) const override;
	virtual bool SetSourceFilename(const UObject* Object, const FString& SourceFilename, int32 SourceIndex) const override;
	virtual bool SetReimportSourceIndex(const UObject* Object, int32 SourceIndex) const override;
	virtual void BackupSourceData(const UObject* Object) const override;
	virtual void ReinstateSourceData(const UObject* Object) const override;
	virtual void ClearBackupSourceData(const UObject* Object) const override;

	// Interchange factory base interface end
	//////////////////////////////////////////////////////////////////////////

	struct FLodPayloads
	{
		TMap<FInterchangeMeshPayLoadKey, TOptional<UE::Interchange::FMeshPayloadData>> MeshPayloadPerKey;
		TMap<FInterchangeMeshPayLoadKey, TOptional<UE::Interchange::FMeshPayloadData>> MorphPayloadPerKey;
	};

private:
	FEvent* SkeletalMeshLockPropertiesEvent = nullptr;

	

	TMap<int32, FLodPayloads> PayloadsPerLodIndex;

	FImportAssetObjectData ImportAssetObjectData;
};


