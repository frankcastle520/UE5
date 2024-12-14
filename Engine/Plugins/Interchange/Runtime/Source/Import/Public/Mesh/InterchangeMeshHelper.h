// Copyright Epic Games, Inc. All Rights Reserved. 
#include "MeshDescription.h"
#include "StaticMeshOperations.h"

struct FSkeletalMaterial;
struct FStaticMaterial;
class UInterchangeBaseNodeContainer;
class UInterchangeSceneNode;

namespace UE::Interchange::Private::MeshHelper
{
	INTERCHANGEIMPORT_API void RemapPolygonGroups(const FMeshDescription& SourceMesh, FMeshDescription& TargetMesh, PolygonGroupMap& RemapPolygonGroup);

	/* return the result of the global transform with the geometric and pivot transform of the scene node. */
	INTERCHANGEIMPORT_API void AddSceneNodeGeometricAndPivotToGlobalTransform(FTransform& GlobalTransform, const UInterchangeSceneNode* SceneNode, const bool bBakeMeshes, const bool bBakePivotMeshes);

	INTERCHANGEIMPORT_API void SkeletalMeshFactorySetupAssetMaterialArray(TArray<FSkeletalMaterial>& ExistMaterials
		, TMap<FString, FString> ImportedSlotMaterialDependencies
		, const UInterchangeBaseNodeContainer* NodeContainer
		, const bool bIsReimport);

	INTERCHANGEIMPORT_API void StaticMeshFactorySetupAssetMaterialArray(TArray<FStaticMaterial>& ExistMaterials
		, TMap<FString, FString> ImportedSlotMaterialDependencies
		, const UInterchangeBaseNodeContainer* NodeContainer
		, const bool bIsReimport);

} //ns UE::Interchange::Private::MeshHelper