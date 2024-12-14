// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/SkeletalMeshImportNode.h"
#include "ChaosClothAsset/ClothDataflowTools.h"
#include "ChaosClothAsset/ClothGeometryTools.h"
#include "ChaosClothAsset/CollectionClothFacade.h"
#include "Animation/Skeleton.h"
#include "Dataflow/DataflowInputOutput.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAssetCommon.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Rendering/SkeletalMeshModel.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SkeletalMeshImportNode)

#define LOCTEXT_NAMESPACE "ChaosClothAssetSkeletalMeshImportNode"

FChaosClothAssetSkeletalMeshImportNode_v2::FChaosClothAssetSkeletalMeshImportNode_v2(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FDataflowNode(InParam, InGuid)
{
	RegisterInputConnection(&SkeletalMesh);
	RegisterOutputConnection(&Collection);
}

void FChaosClothAssetSkeletalMeshImportNode_v2::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	using namespace UE::Chaos::ClothAsset;

	if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		const TSharedRef<FManagedArrayCollection> ClothCollection = MakeShared<FManagedArrayCollection>();
		FCollectionClothFacade ClothFacade(ClothCollection);
		ClothFacade.DefineSchema();

		if (const USkeletalMesh* const InSkeletalMesh = GetValue(Context, &SkeletalMesh))
		{
			const FSkeletalMeshModel* ImportedModel = InSkeletalMesh->GetImportedModel();
			const bool bIsValidLOD = ImportedModel && ImportedModel->LODModels.IsValidIndex(LODIndex);
			if (!bIsValidLOD)
			{
				FClothDataflowTools::LogAndToastWarning(*this, LOCTEXT("InvalidLODHeadline", "Invalid LOD."),
					FText::Format(
						LOCTEXT("InvalidLODDetails", "No valid LOD {0} found for skeletal mesh {1}."),
						LODIndex,
						FText::FromString(InSkeletalMesh->GetName())));

				SetValue(Context, MoveTemp(*ClothCollection), &Collection);
				return;
			}

			const FSkeletalMeshLODModel& LODModel = ImportedModel->LODModels[LODIndex];
			const int32 FirstSection = bImportSingleSection ? SectionIndex : 0;
			const int32 LastSection = bImportSingleSection ? SectionIndex : LODModel.Sections.Num() - 1;
			constexpr bool bImportSimMeshNormals = true;

			for (int32 Section = FirstSection; Section <= LastSection; ++Section)
			{
				const bool bIsValidSection = LODModel.Sections.IsValidIndex(Section);;
				if (!bIsValidSection)
				{
					FClothDataflowTools::LogAndToastWarning(*this, LOCTEXT("InvalidSectionHeadline", "Invalid section."),
						FText::Format(
							LOCTEXT("InvalidSectionDetails", "No valid section {0} found for skeletal mesh {1}."),
							Section,
							FText::FromString(InSkeletalMesh->GetName())));

					continue;
				}

				if (bImportSimMesh)
				{
					FClothDataflowTools::AddSimPatternsFromSkeletalMeshSection(ClothCollection, LODModel, Section, UVChannel, UVScale, bImportSimMeshNormals);
				}

				if (bImportRenderMesh)
				{
					const TArray<FSkeletalMaterial>& Materials = InSkeletalMesh->GetMaterials();
					check(Section < Materials.Num());
					const FString RenderMaterialPathName = Materials[Section].MaterialInterface ? Materials[Section].MaterialInterface->GetPathName() : "";
					FClothDataflowTools::AddRenderPatternFromSkeletalMeshSection(ClothCollection, LODModel, Section, RenderMaterialPathName);
				}
			}

			FClothGeometryTools::CleanupAndCompactMesh(ClothCollection);

			if (const UPhysicsAsset* PhysicsAsset = bSetPhysicsAsset ? InSkeletalMesh->GetPhysicsAsset() : nullptr)
			{
				ClothFacade.SetPhysicsAssetPathName(PhysicsAsset->GetPathName());
			}

			ClothFacade.SetSkeletalMeshPathName(InSkeletalMesh->GetPathName());
		}
		SetValue(Context, MoveTemp(*ClothCollection), &Collection);
	}
}



FChaosClothAssetSkeletalMeshImportNode::FChaosClothAssetSkeletalMeshImportNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FDataflowNode(InParam, InGuid)
{
	RegisterInputConnection(&SkeletalMesh);
	RegisterOutputConnection(&Collection);
}

void FChaosClothAssetSkeletalMeshImportNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	using namespace UE::Chaos::ClothAsset;

	if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		const TSharedRef<FManagedArrayCollection> ClothCollection = MakeShared<FManagedArrayCollection>();
		FCollectionClothFacade ClothFacade(ClothCollection);
		ClothFacade.DefineSchema();

		if (const USkeletalMesh* const InSkeletalMesh = GetValue(Context, &SkeletalMesh))
		{
			const FSkeletalMeshModel* ImportedModel = InSkeletalMesh->GetImportedModel();
			const bool bIsValidLOD = ImportedModel && ImportedModel->LODModels.IsValidIndex(LODIndex);
			if (!bIsValidLOD)
			{
				FClothDataflowTools::LogAndToastWarning(*this, LOCTEXT("InvalidLODHeadline", "Invalid LOD."),
					FText::Format(
						LOCTEXT("InvalidLODDetails", "No valid LOD {0} found for skeletal mesh {1}."),
						LODIndex,
						FText::FromString(InSkeletalMesh->GetName())));

				SetValue(Context, MoveTemp(*ClothCollection), &Collection);
				return;
			}

			const FSkeletalMeshLODModel &LODModel = ImportedModel->LODModels[LODIndex];
			const int32 FirstSection = bImportSingleSection ? SectionIndex : 0;
			const int32 LastSection = bImportSingleSection ? SectionIndex : LODModel.Sections.Num() - 1;
			constexpr bool bImportSimMeshNormals = false;

			for (int32 Section = FirstSection; Section <= LastSection; ++Section)
			{
				const bool bIsValidSection = LODModel.Sections.IsValidIndex(Section);;
				if (!bIsValidSection)
				{
					FClothDataflowTools::LogAndToastWarning(*this, LOCTEXT("InvalidSectionHeadline", "Invalid section."),
						FText::Format(
							LOCTEXT("InvalidSectionDetails", "No valid section {0} found for skeletal mesh {1}."),
							Section,
							FText::FromString(InSkeletalMesh->GetName())));

					continue;
				}

				if (bImportSimMesh)
				{
					FClothDataflowTools::AddSimPatternsFromSkeletalMeshSection(ClothCollection, LODModel, Section, UVChannel, UVScale, bImportSimMeshNormals);
				}

				if (bImportRenderMesh)
				{
					const TArray<FSkeletalMaterial>& Materials = InSkeletalMesh->GetMaterials();
					check(Section < Materials.Num());
					const FString RenderMaterialPathName = Materials[Section].MaterialInterface ? Materials[Section].MaterialInterface->GetPathName() : "";
					FClothDataflowTools::AddRenderPatternFromSkeletalMeshSection(ClothCollection, LODModel, Section, RenderMaterialPathName);
				}
			}

			if (const UPhysicsAsset* PhysicsAsset = bSetPhysicsAsset ? InSkeletalMesh->GetPhysicsAsset() : nullptr)
			{
				ClothFacade.SetPhysicsAssetPathName(PhysicsAsset->GetPathName());
			}

			// In order to retain existing behavior, flip the sim normals.
			constexpr bool bReverseSimMeshNormals = true;
			constexpr bool bReverseFalse = false;
			FClothGeometryTools::ReverseMesh(ClothCollection, bReverseSimMeshNormals, bReverseFalse, bReverseFalse, bReverseFalse, TArray<int32>(), TArray<int32>());

			ClothFacade.SetSkeletalMeshPathName(InSkeletalMesh->GetPathName());
		}
		SetValue(Context, MoveTemp(*ClothCollection), &Collection);
	}
}

void FChaosClothAssetSkeletalMeshImportNode::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FFortniteMainBranchObjectVersion::GUID);
	if (Ar.IsLoading() && Ar.CustomVer(FFortniteMainBranchObjectVersion::GUID) < FFortniteMainBranchObjectVersion::ClothAssetSkeletalMeshMultiSectionImport)
	{
		bImportSingleSection = true;
		bSetPhysicsAsset = true;
	}
}

#undef LOCTEXT_NAMESPACE
