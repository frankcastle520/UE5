// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InterchangeFactoryBase.h"
#include "InterchangeMeshDefinitions.h"
#include "InterchangeMeshNode.h"
#include "Mesh/InterchangeMeshPayload.h"
#include "MeshDescription.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "InterchangeStaticMeshFactory.generated.h"


class UBodySetup;
class UStaticMesh;
class UInterchangeStaticMeshFactoryNode;
class UInterchangeStaticMeshLodDataNode;
struct FKAggregateGeom;

UCLASS(BlueprintType)
class INTERCHANGEIMPORT_API UInterchangeStaticMeshFactory : public UInterchangeFactoryBase
{
	GENERATED_BODY()
public:

	//////////////////////////////////////////////////////////////////////////
	// Interchange factory base interface begin

	virtual UClass* GetFactoryClass() const override;
	virtual EInterchangeFactoryAssetType GetFactoryAssetType() override { return EInterchangeFactoryAssetType::Meshes; }
	virtual void CreatePayloadTasks(const FImportAssetObjectParams& Arguments, bool bAsync, TArray<TSharedPtr<UE::Interchange::FInterchangeTaskBase>>& PayloadTasks) override;
	virtual FImportAssetResult BeginImportAsset_GameThread(const FImportAssetObjectParams& Arguments) override;
	virtual FImportAssetResult ImportAsset_Async(const FImportAssetObjectParams& Arguments) override;
	virtual FImportAssetResult EndImportAsset_GameThread(const FImportAssetObjectParams& Arguments) override;
	virtual void SetupObject_GameThread(const FSetupObjectParams& Arguments) override;
	virtual void BuildObject_GameThread(const FSetupObjectParams& Arguments, bool& OutPostEditchangeCalled) override;
	virtual bool GetSourceFilenames(const UObject* Object, TArray<FString>& OutSourceFilenames) const override;
	virtual bool SetSourceFilename(const UObject* Object, const FString& SourceFilename, int32 SourceIndex) const override;
	virtual void BackupSourceData(const UObject* Object) const override;
	virtual void ReinstateSourceData(const UObject* Object) const override;
	virtual void ClearBackupSourceData(const UObject* Object) const override;

	// Interchange factory base interface end
	//////////////////////////////////////////////////////////////////////////

private:
	struct FMeshPayload
	{
		FString MeshName;
		TOptional<UE::Interchange::FMeshPayloadData> PayloadData;
		FTransform Transform = FTransform::Identity;
	};

	struct FLodPayloads
	{
		TMap<FInterchangeMeshPayLoadKey, FMeshPayload> MeshPayloadPerKey;
		TMap<FInterchangeMeshPayLoadKey, FMeshPayload> CollisionBoxPayloadPerKey;
		TMap<FInterchangeMeshPayLoadKey, FMeshPayload> CollisionCapsulePayloadPerKey;
		TMap<FInterchangeMeshPayLoadKey, FMeshPayload> CollisionSpherePayloadPerKey;
		TMap<FInterchangeMeshPayLoadKey, FMeshPayload> CollisionConvexPayloadPerKey;
	};

	TMap<int32, FLodPayloads> PayloadsPerLodIndex;

	bool AddConvexGeomFromVertices(const FImportAssetObjectParams& Arguments, const FMeshDescription& MeshDescription, const FTransform& Transform, FKAggregateGeom& AggGeom);
	bool DecomposeConvexMesh(const FImportAssetObjectParams& Arguments, const FMeshDescription& MeshDescription, const FTransform& Transform, UBodySetup* BodySetup);
	bool AddBoxGeomFromTris(const FMeshDescription& MeshDescription, const FTransform& Transform, FKAggregateGeom& AggGeom);
	bool AddSphereGeomFromVertices(const FImportAssetObjectParams& Arguments, const FMeshDescription& MeshDescription, const FTransform& Transform, FKAggregateGeom& AggGeom);
	bool AddCapsuleGeomFromVertices(const FImportAssetObjectParams& Arguments, const FMeshDescription& MeshDescription, const FTransform& Transform, FKAggregateGeom& AggGeom);

	bool ImportBoxCollision(const FImportAssetObjectParams& Arguments, UStaticMesh* StaticMesh);
	bool ImportCapsuleCollision(const FImportAssetObjectParams& Arguments, UStaticMesh* StaticMesh);
	bool ImportSphereCollision(const FImportAssetObjectParams& Arguments, UStaticMesh* StaticMesh);
	bool ImportConvexCollision(const FImportAssetObjectParams& Arguments, UStaticMesh* StaticMesh, const UInterchangeStaticMeshLodDataNode* LodDataNode);
	bool ImportSockets(const FImportAssetObjectParams& Arguments, UStaticMesh* StaticMesh, const UInterchangeStaticMeshFactoryNode* FactoryNode);

	void CommitMeshDescriptions(UStaticMesh& StaticMesh);
	void BuildFromMeshDescriptions(UStaticMesh& StaticMesh);

#if WITH_EDITORONLY_DATA
	void SetupSourceModelsSettings(UStaticMesh& StaticMesh, const TArray<FMeshDescription>& LodMeshDescriptions, bool bAutoComputeLODScreenSizes, const TArray<float>& LodScreenSizes, int32 PreviousLodCount, int32 FinalLodCount, bool bIsAReimport);
#endif
	struct FImportAssetObjectData
	{
		TArray<FMeshDescription> LodMeshDescriptions;
		EInterchangeMeshCollision Collision = EInterchangeMeshCollision::None;
		FKAggregateGeom AggregateGeom;
		bool bIsAppGame = false;
		bool bImportedCustomCollision = false;
		bool bImportCollision = false;
	};
	FImportAssetObjectData ImportAssetObjectData;
};
