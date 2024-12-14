// Copyright Epic Games, Inc. All Rights Reserved. 
#include "Mesh/InterchangeMeshHelper.h"

#include "Engine/SkinnedAssetCommon.h"
#include "Engine/StaticMesh.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "InterchangeHelper.h"
#include "InterchangeMaterialFactoryNode.h"
#include "InterchangeSceneNode.h"
#include "MaterialDomain.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "MeshUVChannelInfo.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshOperations.h"


namespace UE::Interchange::Private::MeshHelper
{
	void RemapPolygonGroups(const FMeshDescription& SourceMesh, FMeshDescription& TargetMesh, PolygonGroupMap& RemapPolygonGroup)
	{
		FStaticMeshConstAttributes SourceAttributes(SourceMesh);
		TPolygonGroupAttributesConstRef<FName> SourceImportedMaterialSlotNames = SourceAttributes.GetPolygonGroupMaterialSlotNames();

		FStaticMeshAttributes TargetAttributes(TargetMesh);
		TPolygonGroupAttributesRef<FName> TargetImportedMaterialSlotNames = TargetAttributes.GetPolygonGroupMaterialSlotNames();

		for (FPolygonGroupID SourcePolygonGroupID : SourceMesh.PolygonGroups().GetElementIDs())
		{
			FPolygonGroupID TargetMatchingID = INDEX_NONE;
			for (FPolygonGroupID TargetPolygonGroupID : TargetMesh.PolygonGroups().GetElementIDs())
			{
				if (SourceImportedMaterialSlotNames[SourcePolygonGroupID] == TargetImportedMaterialSlotNames[TargetPolygonGroupID])
				{
					TargetMatchingID = TargetPolygonGroupID;
					break;
				}
			}
			if (TargetMatchingID == INDEX_NONE)
			{
				TargetMatchingID = TargetMesh.CreatePolygonGroup();
				TargetImportedMaterialSlotNames[TargetMatchingID] = SourceImportedMaterialSlotNames[SourcePolygonGroupID];
			}
			else
			{
				//Since we want to keep the sections separate we need to create a new polygongroup
				TargetMatchingID = TargetMesh.CreatePolygonGroup();
				FString NewSlotName = SourceImportedMaterialSlotNames[SourcePolygonGroupID].ToString() + TEXT("_Section") + FString::FromInt(TargetMatchingID.GetValue());
				TargetImportedMaterialSlotNames[TargetMatchingID] = FName(NewSlotName);
			}
			RemapPolygonGroup.Add(SourcePolygonGroupID, TargetMatchingID);
		}
	}

	void AddSceneNodeGeometricAndPivotToGlobalTransform(FTransform& GlobalTransform, const UInterchangeSceneNode* SceneNode, const bool bBakeMeshes, const bool bBakePivotMeshes)
	{
		FTransform SceneNodeGeometricTransform;
		SceneNode->GetCustomGeometricTransform(SceneNodeGeometricTransform);

		if (!bBakeMeshes)
		{
			if (bBakePivotMeshes)
			{
				FTransform SceneNodePivotNodeTransform;
				if (SceneNode->GetCustomPivotNodeTransform(SceneNodePivotNodeTransform))
				{
					SceneNodeGeometricTransform = SceneNodePivotNodeTransform * SceneNodeGeometricTransform;
				}
			}
			else
			{
				SceneNodeGeometricTransform.SetIdentity();
			}
		}
		GlobalTransform = bBakeMeshes ? SceneNodeGeometricTransform * GlobalTransform : SceneNodeGeometricTransform;
	}

	template<typename MaterialType>
	class INTERCHANGEIMPORT_API FMeshMaterialViewer
	{
	public:
		FMeshMaterialViewer(TArray<MaterialType>& InMaterials, int32 InMaterialIndex)
			: Materials(InMaterials)
			, MaterialIndex(InMaterialIndex)
		{ }

		FName GetMaterialSlotName() const
		{
			if (Materials.IsValidIndex(MaterialIndex))
			{
				return Materials[MaterialIndex].MaterialSlotName;
			}
			return NAME_None;
		}

		FName GetImportedMaterialSlotName() const
		{
#if WITH_EDITOR
			if (Materials.IsValidIndex(MaterialIndex))
			{
				return Materials[MaterialIndex].ImportedMaterialSlotName;
			}
			return NAME_None;
#else
			return GetMaterialSlotName();
#endif
		}

		void SetMaterialSlotName(const FName MaterialSlotName)
		{
			if (Materials.IsValidIndex(MaterialIndex))
			{
				Materials[MaterialIndex].MaterialSlotName = MaterialSlotName;
			}
		}

		void SetImportedMaterialSlotName(const FName ImportedMaterialSlotName)
		{
#if WITH_EDITOR
			if (Materials.IsValidIndex(MaterialIndex))
			{
				Materials[MaterialIndex].ImportedMaterialSlotName = ImportedMaterialSlotName;
			}
#endif
		}

		UMaterialInterface* GetMaterialInterface() const
		{
			if (Materials.IsValidIndex(MaterialIndex))
			{
				return Materials[MaterialIndex].MaterialInterface;
			}
			return nullptr;
		}

		void SetMaterialInterface(UMaterialInterface* MaterialInterface)
		{
			if (Materials.IsValidIndex(MaterialIndex))
			{
				Materials[MaterialIndex].MaterialInterface = MaterialInterface;
			}
		}
	private:
		TArray<MaterialType>& Materials;
		int32 MaterialIndex = INDEX_NONE;
	};

	template<typename MaterialType>
	class INTERCHANGEIMPORT_API FMeshMaterialArrayViewer
	{
	public:
		FMeshMaterialArrayViewer(TArray<MaterialType>& InMaterials, TFunction<void(MaterialType& Material)>& InEmplaceMaterialFunctor)
			: Materials(InMaterials)
			, EmplaceMaterialFunctor(InEmplaceMaterialFunctor)
		{
			RebuildViewer();
		}

		void RebuildViewer()
		{
			MeshMaterialArrayViewer.Reset(Materials.Num());
			for (int32 MaterialIndex = 0; MaterialIndex < Materials.Num(); ++MaterialIndex)
			{
				MeshMaterialArrayViewer.Emplace(Materials, MaterialIndex);
			}
		}

		int32 Num() const
		{
			return MeshMaterialArrayViewer.Num();
		}

		FMeshMaterialViewer<MaterialType>& operator[](int32 MaterialIndex)
		{
			check(MeshMaterialArrayViewer.IsValidIndex(MaterialIndex));
			return MeshMaterialArrayViewer[MaterialIndex];
		}

		FMeshMaterialViewer<MaterialType>* FindByPredicate(TFunction<bool(const FMeshMaterialViewer<MaterialType>& MaterialViewer)> Predicate)
		{
			return MeshMaterialArrayViewer.FindByPredicate(Predicate);
		}

		void Emplace(UMaterialInterface* NewMaterial, FName MaterialSlotName, FName ImportedMaterialSlotName)
		{
			MaterialType& Material = Materials.AddDefaulted_GetRef();
			Material.MaterialInterface = NewMaterial;
			Material.MaterialSlotName = MaterialSlotName;
#if WITH_EDITOR
			Material.ImportedMaterialSlotName = ImportedMaterialSlotName;
#endif

			EmplaceMaterialFunctor(Material);

			RebuildViewer();
		}

		void Reserve(int32 Count)
		{
			Materials.Reserve(Count);

			RebuildViewer();
		}

	private:
		TArray<MaterialType>& Materials;
		TFunction<void(MaterialType& Material)>& EmplaceMaterialFunctor;
		TArray<FMeshMaterialViewer<MaterialType>> MeshMaterialArrayViewer;
	};

	template<typename MaterialType>
	void InternalMeshFactorySetupAssetMaterialArray(FMeshMaterialArrayViewer<MaterialType>& ExistMaterialArrayViewer
		, TMap<FString, FString> ImportedSlotMaterialDependencies
		, const UInterchangeBaseNodeContainer* NodeContainer
		, const bool bIsReimport)
	{
		//Store the existing material index that match with the imported material
		TArray<int32> ImportedMaterialMatchExistingMaterialIndex;
		//Store the imported material index that match with the existing material
		TArray<int32> ExistingMaterialMatchImportedMaterialIndex;

		const int32 ImportedMaterialCount = ImportedSlotMaterialDependencies.Num();
		const int32 ExistingMaterialCount = ExistMaterialArrayViewer.Num();

		//Find which imported slot material match with existing slot material
		{
			ImportedMaterialMatchExistingMaterialIndex.SetNum(ImportedMaterialCount);
			for (int32 ImportedMaterialIndex = 0; ImportedMaterialIndex < ImportedMaterialCount; ++ImportedMaterialIndex)
			{
				ImportedMaterialMatchExistingMaterialIndex[ImportedMaterialIndex] = INDEX_NONE;
			}

			ExistingMaterialMatchImportedMaterialIndex.SetNum(ExistingMaterialCount);
			for (int32 ExistingMaterialIndex = 0; ExistingMaterialIndex < ExistingMaterialCount; ++ExistingMaterialIndex)
			{
				ExistingMaterialMatchImportedMaterialIndex[ExistingMaterialIndex] = INDEX_NONE;
			}

			int32 ImportedMaterialIndex = 0;
			for (TPair<FString, FString>& SlotMaterialDependency : ImportedSlotMaterialDependencies)
			{
				FName MaterialSlotName = *SlotMaterialDependency.Key;
				for (int32 ExistingMaterialIndex = 0; ExistingMaterialIndex < ExistingMaterialCount; ++ExistingMaterialIndex)
				{
					if (ExistingMaterialMatchImportedMaterialIndex[ExistingMaterialIndex] != INDEX_NONE)
					{
						continue;
					}

					const FMeshMaterialViewer<MaterialType>& Material = ExistMaterialArrayViewer[ExistingMaterialIndex];
					if (Material.GetMaterialSlotName() == MaterialSlotName)
					{
						ExistingMaterialMatchImportedMaterialIndex[ExistingMaterialIndex] = ImportedMaterialIndex;
						ImportedMaterialMatchExistingMaterialIndex[ImportedMaterialIndex] = ExistingMaterialIndex;
						break;
					}
				}
				ImportedMaterialIndex++;
			}
		}


		auto UpdateOrAddMaterial = [&ExistMaterialArrayViewer
			, bIsReimport
			, &ImportedMaterialMatchExistingMaterialIndex
			, &ExistingMaterialMatchImportedMaterialIndex
			, &ExistingMaterialCount]
			(const FName& MaterialSlotName, UMaterialInterface* MaterialInterface, const int32 ImportedMaterialIndex)
			{
				UMaterialInterface* NewMaterial = MaterialInterface ? MaterialInterface : UMaterial::GetDefaultMaterial(MD_Surface);

				FMeshMaterialViewer<MaterialType>* MeshMaterialViewer = ExistMaterialArrayViewer.FindByPredicate([&MaterialSlotName](const FMeshMaterialViewer<MaterialType>& Material) { return Material.GetMaterialSlotName() == MaterialSlotName; });
				if (MeshMaterialViewer)
				{
					//When we are not re-importing, we always force update the material, we should see this case when importing LODs is on since its an import.
					//When we do a re-import we update the material interface only if the current asset matching material is null and is not the default material.
					if (!bIsReimport || (MaterialInterface && (!MeshMaterialViewer->GetMaterialInterface() || MeshMaterialViewer->GetMaterialInterface() == UMaterial::GetDefaultMaterial(MD_Surface))))
					{
						MeshMaterialViewer->SetMaterialInterface(NewMaterial);
					}
				}
				else
				{
					//See if we can pick and unmatched existing material slot before creating one
					bool bCreateNewMaterialSlot = true;
					for (int32 ExistingMaterialIndex = 0; ExistingMaterialIndex < ExistingMaterialCount; ++ExistingMaterialIndex)
					{
						//Find the next available unmatched existing material slot and pick it up instead of creating a new material
						if (ExistingMaterialMatchImportedMaterialIndex[ExistingMaterialIndex] == INDEX_NONE)
						{
							bCreateNewMaterialSlot = false;
							FMeshMaterialViewer<MaterialType>& ExistingSkeletalMaterial = ExistMaterialArrayViewer[ExistingMaterialIndex];
							ExistingSkeletalMaterial.SetMaterialSlotName(MaterialSlotName);
							ExistingSkeletalMaterial.SetImportedMaterialSlotName(MaterialSlotName);
							ExistingMaterialMatchImportedMaterialIndex[ExistingMaterialIndex] = ImportedMaterialIndex;
							ImportedMaterialMatchExistingMaterialIndex[ImportedMaterialIndex] = ExistingMaterialIndex;
							break;
						}
					}
					if (bCreateNewMaterialSlot)
					{
						ExistMaterialArrayViewer.Emplace(NewMaterial, MaterialSlotName, MaterialSlotName);
					}
				}
			};

		//Preallocate the extra memory if needed
		if (ImportedMaterialCount > ExistingMaterialCount)
		{
			ExistMaterialArrayViewer.Reserve(ImportedMaterialCount);
		}

		int32 ImportedMaterialIndex = 0;
		for (TPair<FString, FString>& SlotMaterialDependency : ImportedSlotMaterialDependencies)
		{
			UE::Interchange::FScopedLambda ScopedLambda([&ImportedMaterialIndex]()
				{
					++ImportedMaterialIndex;
				});
			FName MaterialSlotName = *SlotMaterialDependency.Key;

			const UInterchangeBaseMaterialFactoryNode* MaterialFactoryNode = Cast<UInterchangeBaseMaterialFactoryNode>(NodeContainer->GetNode(SlotMaterialDependency.Value));
			if (!MaterialFactoryNode)
			{
				UpdateOrAddMaterial(MaterialSlotName, nullptr, ImportedMaterialIndex);
				continue;
			}

			FSoftObjectPath MaterialFactoryNodeReferenceObject;
			MaterialFactoryNode->GetCustomReferenceObject(MaterialFactoryNodeReferenceObject);
			if (!MaterialFactoryNodeReferenceObject.IsValid())
			{
				UpdateOrAddMaterial(MaterialSlotName, nullptr, ImportedMaterialIndex);
				continue;
			}

			UMaterialInterface* MaterialInterface = Cast<UMaterialInterface>(MaterialFactoryNodeReferenceObject.ResolveObject());
			UpdateOrAddMaterial(MaterialSlotName, MaterialInterface ? MaterialInterface : nullptr, ImportedMaterialIndex);
		}
	}

	void SkeletalMeshFactorySetupAssetMaterialArray(TArray<FSkeletalMaterial>& ExistMaterials
		, TMap<FString, FString> ImportedSlotMaterialDependencies
		, const UInterchangeBaseNodeContainer* NodeContainer
		, const bool bIsReimport)
	{
		TFunction<void(FSkeletalMaterial&)> EmplaceMaterialFunctor = [](FSkeletalMaterial& Material) { };
		FMeshMaterialArrayViewer<FSkeletalMaterial> MeshMaterialArrayViewer(ExistMaterials, EmplaceMaterialFunctor);
		InternalMeshFactorySetupAssetMaterialArray<FSkeletalMaterial>(MeshMaterialArrayViewer, ImportedSlotMaterialDependencies, NodeContainer, bIsReimport);
	}

	void StaticMeshFactorySetupAssetMaterialArray(TArray<FStaticMaterial>& ExistMaterials
		, TMap<FString, FString> ImportedSlotMaterialDependencies
		, const UInterchangeBaseNodeContainer* NodeContainer
		, const bool bIsReimport)
	{
		TFunction<void(FStaticMaterial&)> EmplaceMaterialFunctor = [](FStaticMaterial& Material)
			{
#if !WITH_EDITOR
				// UV density is not supported to be generated at runtime for now. We fake that it has been initialized so that we don't trigger ensures.
				Material.UVChannelData = FMeshUVChannelInfo(1.f);
#endif
			};
		FMeshMaterialArrayViewer<FStaticMaterial> MeshMaterialArrayViewer(ExistMaterials, EmplaceMaterialFunctor);
		InternalMeshFactorySetupAssetMaterialArray<FStaticMaterial>(MeshMaterialArrayViewer, ImportedSlotMaterialDependencies, NodeContainer, bIsReimport);
	}

} //ns UE::Interchange::Private::MeshHelper