// Copyright Epic Games, Inc. All Rights Reserved.

#include "SkeletalMeshBuilder.h"
#include "Modules/ModuleManager.h"
#include "MeshBoneReduction.h"
#include "Engine/EngineTypes.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAssetCommon.h"
#include "PhysicsEngine/BodySetup.h"
#include "MeshDescription.h"
#include "MeshAttributes.h"
#include "MeshDescriptionHelper.h"
#include "MeshBuild.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "GPUSkinVertexFactory.h"
#include "ThirdPartyBuildOptimizationHelper.h"
#include "Misc/ScopedSlowTask.h"
#include "LODUtilities.h"
#include "ClothingAsset.h"
#include "MeshUtilities.h"
#include "EditorFramework/AssetImportData.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Misc/CoreMisc.h"
#include "NaniteBuilder.h"
#include "Rendering/NaniteResources.h"

DEFINE_LOG_CATEGORY(LogSkeletalMeshBuilder);

namespace SkeletalMeshBuilderOptimization
{
	void CacheOptimizeIndexBuffer(TArray<uint16>& Indices)
	{
		BuildOptimizationThirdParty::CacheOptimizeIndexBuffer(Indices);
	}

	void CacheOptimizeIndexBuffer(TArray<uint32>& Indices)
	{
		BuildOptimizationThirdParty::CacheOptimizeIndexBuffer(Indices);
	}
}

struct InfluenceMap
{
	FORCEINLINE bool operator()(const float& A, const float& B) const
	{
		return B > A;
	}
};

struct FSkeletalMeshVertInstanceIDAndZ
{
	FVertexInstanceID Index;
	float Z;
};

FSkeletalMeshBuilder::FSkeletalMeshBuilder()
{
}

static bool BuildNanite(
	USkeletalMesh* SkeletalMesh,
	FSkeletalMeshLODModel& LODModel,
	const FMeshDescription& MeshDescription,
	Nanite::FResources& NaniteResources
)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FStaticMeshBuilder::BuildNanite);

	Nanite::IBuilderModule& NaniteBuilderModule = Nanite::IBuilderModule::Get();

	// Build new vertex buffers
	Nanite::IBuilderModule::FInputMeshData InputMeshData;

	InputMeshData.NumTexCoords = LODModel.NumTexCoords;

	InputMeshData.MaterialIndices.SetNumUninitialized(LODModel.IndexBuffer.Num() / 3);

	InputMeshData.Vertices.Position.SetNumUninitialized(LODModel.NumVertices);
	InputMeshData.Vertices.TangentX.SetNumUninitialized(LODModel.NumVertices);
	InputMeshData.Vertices.TangentY.SetNumUninitialized(LODModel.NumVertices);
	InputMeshData.Vertices.TangentZ.SetNumUninitialized(LODModel.NumVertices);

	InputMeshData.Vertices.UVs.SetNum(LODModel.NumTexCoords);
	for (uint32 UVCoord = 0; UVCoord < LODModel.NumTexCoords; ++UVCoord)
	{
		InputMeshData.Vertices.UVs[UVCoord].SetNumUninitialized(LODModel.NumVertices);
	}

	// We can save memory by figuring out the max number of influences across all sections instead of allocating MAX_TOTAL_INFLUENCES
	// Also check if any of the sections actually require 16bit, or if 8bit will suffice
	bool b16BitSkinning = false;
	InputMeshData.NumBoneInfluences = 0;
	for (const FSkelMeshSection& Section : LODModel.Sections)
	{
		InputMeshData.NumBoneInfluences = FMath::Max(InputMeshData.NumBoneInfluences, uint32(Section.MaxBoneInfluences));
		b16BitSkinning |= Section.Use16BitBoneIndex();
	}

	InputMeshData.Vertices.BoneIndices.SetNum(InputMeshData.NumBoneInfluences);
	InputMeshData.Vertices.BoneWeights.SetNum(InputMeshData.NumBoneInfluences);
	for (uint32 Influence = 0; Influence < InputMeshData.NumBoneInfluences; ++Influence)
	{
		InputMeshData.Vertices.BoneIndices[Influence].SetNumZeroed(LODModel.NumVertices);
		InputMeshData.Vertices.BoneWeights[Influence].SetNumZeroed(LODModel.NumVertices);
	}

	// TODO: Nanite-Skinning
	//InputMeshData.Vertices.Color.SetNumUninitialized(LODModel.NumVertices);

	InputMeshData.TriangleIndices = LODModel.IndexBuffer;

	uint32 CheckIndices = 0;
	uint32 CheckVertices = 0;

	for (int32 SectionIndex = 0; SectionIndex < LODModel.Sections.Num(); ++SectionIndex)
	{
		const FSkelMeshSection& Section = LODModel.Sections[SectionIndex];

		check(CheckIndices  == Section.BaseIndex);
		check(CheckVertices == Section.BaseVertexIndex);

		for (int32 VertIndex = 0; VertIndex < Section.SoftVertices.Num(); ++VertIndex)
		{
			const FSoftSkinVertex& SoftVertex = Section.SoftVertices[VertIndex];

			InputMeshData.Vertices.Position[Section.BaseVertexIndex + VertIndex] = SoftVertex.Position;
			InputMeshData.Vertices.TangentX[Section.BaseVertexIndex + VertIndex] = SoftVertex.TangentX;
			InputMeshData.Vertices.TangentY[Section.BaseVertexIndex + VertIndex] = SoftVertex.TangentY;
			InputMeshData.Vertices.TangentZ[Section.BaseVertexIndex + VertIndex] = SoftVertex.TangentZ;

			InputMeshData.VertexBounds += SoftVertex.Position;

			for (uint32 UVCoord = 0; UVCoord < LODModel.NumTexCoords; ++UVCoord)
			{
				InputMeshData.Vertices.UVs[UVCoord][Section.BaseVertexIndex + VertIndex] = SoftVertex.UVs[UVCoord];
			}

			for (int32 Influence = 0; Influence < Section.MaxBoneInfluences; ++Influence)
			{
				InputMeshData.Vertices.BoneIndices[Influence][Section.BaseVertexIndex + VertIndex] = Section.BoneMap[SoftVertex.InfluenceBones[Influence]];
				InputMeshData.Vertices.BoneWeights[Influence][Section.BaseVertexIndex + VertIndex] = SoftVertex.InfluenceWeights[Influence];
			}

			//InputMeshData.Vertices.Color[Section.BaseVertexIndex + VertIndex] = SoftVertex.Color;
		}

		for (uint32 MaterialIndex = 0; MaterialIndex < Section.NumTriangles; ++MaterialIndex)
		{
			InputMeshData.MaterialIndices[(CheckIndices / 3) + MaterialIndex] = Section.MaterialIndex;
		}

		CheckIndices += Section.NumTriangles * 3;
		CheckVertices += Section.NumVertices;
	}

	check(CheckVertices == LODModel.NumVertices);
	check(CheckIndices == LODModel.IndexBuffer.Num());

	InputMeshData.TriangleCounts.Add(LODModel.IndexBuffer.Num() / 3);

	auto OnFreeInputMeshData = Nanite::IBuilderModule::FOnFreeInputMeshData::CreateLambda([&InputMeshData](bool bFallbackIsReduced)
	{
		if (bFallbackIsReduced)
		{
			InputMeshData.Vertices.Empty();
			InputMeshData.TriangleIndices.Empty();
		}

		InputMeshData.MaterialIndices.Empty();
	});

	FMeshNaniteSettings NaniteSettings = SkeletalMesh->NaniteSettings;
	NaniteSettings.KeepPercentTriangles = 1.0f;
	NaniteSettings.TrimRelativeError = 0.0f;
	NaniteSettings.FallbackPercentTriangles = 1.0f; // 100% - no reduction
	NaniteSettings.FallbackRelativeError = 0.0f;

	if (!NaniteBuilderModule.Build(
		NaniteResources,
		InputMeshData,
		nullptr, // OutFallbackMeshData
		NaniteSettings,
		OnFreeInputMeshData))
	{
		UE_LOG(LogStaticMesh, Error, TEXT("Failed to build Nanite for skeletal mesh. See previous line(s) for details."));
		return false;
	}

	return true;
}

bool FSkeletalMeshBuilder::Build(const FSkeletalMeshBuildParameters& SkeletalMeshBuildParameters)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FSkeletalMeshBuilder::Build);
	const int32 LODIndex = SkeletalMeshBuildParameters.LODIndex;
	USkeletalMesh* SkeletalMesh = SkeletalMeshBuildParameters.SkeletalMesh;

	check(SkeletalMesh->GetImportedModel());
	check(SkeletalMesh->GetImportedModel()->LODModels.IsValidIndex(LODIndex));
	check(SkeletalMesh->GetLODInfo(LODIndex) != nullptr);
	
	const FSkeletalMeshLODInfo* LODInfo = SkeletalMesh->GetLODInfo(LODIndex);
	//We want to backup in case the LODModel is regenerated, this data is use to validate in the UI if the ddc must be rebuild
	const FString BackupBuildStringID = SkeletalMesh->GetImportedModel()->LODModels[LODIndex].BuildStringID;

	const FReferenceSkeleton& RefSkeleton = SkeletalMesh->GetRefSkeleton();

	const bool bNaniteBuildEnabled = SkeletalMesh->IsNaniteEnabled();

	FScopedSlowTask SlowTask(bNaniteBuildEnabled ? 7.01f : 6.01f, NSLOCTEXT("SkeltalMeshBuilder", "BuildingSkeletalMeshLOD", "Building skeletal mesh LOD"));
	SlowTask.MakeDialog();

	//Prevent any PostEdit change during the build
	FScopedSkeletalMeshPostEditChange ScopedPostEditChange(SkeletalMesh, false, false);
	// Unbind any existing clothing assets before we reimport the geometry
	TArray<ClothingAssetUtils::FClothingAssetMeshBinding> ClothingBindings;
	FLODUtilities::UnbindClothingAndBackup(SkeletalMesh, ClothingBindings, LODIndex);

	int32 NumTexCoord = 1; //We need to send rendering at least one tex coord buffer

	//This scope define where we can use the LODModel, after a reduction the LODModel must be requery since it is a new instance
	{
		FSkeletalMeshLODModel& BuildLODModel = SkeletalMesh->GetImportedModel()->LODModels[LODIndex];

		//Load the imported data
		const FMeshDescription& SkeletalMeshModel = *SkeletalMesh->GetMeshDescription(LODIndex);
		FSkeletalMeshImportData SkeletalMeshImportData = FSkeletalMeshImportData::CreateFromMeshDescription(SkeletalMeshModel);

		TArray<FVector3f> LODPoints;
		TArray<SkeletalMeshImportData::FMeshWedge> LODWedges;
		TArray<SkeletalMeshImportData::FMeshFace> LODFaces;
		TArray<SkeletalMeshImportData::FVertInfluence> LODInfluences;
		TArray<int32> LODPointToRawMap;
		SkeletalMeshImportData.CopyLODImportData(LODPoints, LODWedges, LODFaces, LODInfluences, LODPointToRawMap);

		//Use the max because we need to have at least one texture coordinate
		NumTexCoord = FMath::Max<int32>(NumTexCoord, SkeletalMeshImportData.NumTexCoords);
		
		// BaseLOD needs to make sure the source data fit with the skeletal mesh materials array before using meshutilities.BuildSkeletalMesh
		FLODUtilities::AdjustImportDataFaceMaterialIndex(SkeletalMesh->GetMaterials(), SkeletalMeshImportData.Materials, LODFaces, LODIndex);

		// Build the skeletal mesh using mesh utilities module
		IMeshUtilities::MeshBuildOptions Options;
		Options.FillOptions(LODInfo->BuildSettings);
		Options.TargetPlatform = SkeletalMeshBuildParameters.TargetPlatform;
		// Force the normals or tangent in case the data is missing
		Options.bComputeNormals |= !SkeletalMeshImportData.bHasNormals;
		Options.bComputeTangents |= !SkeletalMeshImportData.bHasTangents;
		IMeshUtilities& MeshUtilities = FModuleManager::Get().LoadModuleChecked<IMeshUtilities>("MeshUtilities");

		// Create skinning streams for NewModel.
		SlowTask.EnterProgressFrame(1.0f);
		MeshUtilities.BuildSkeletalMesh(
			BuildLODModel, 
			SkeletalMesh->GetPathName(),
			RefSkeleton,
			LODInfluences,
			LODWedges,
			LODFaces,
			LODPoints,
			LODPointToRawMap,
			Options
		);

		// Set texture coordinate count on the new model.
		BuildLODModel.NumTexCoords = NumTexCoord;

		// Cache the vertex/triangle count in the InlineReductionCacheData so we can know if the LODModel need reduction or not.
		TArray<FInlineReductionCacheData>& InlineReductionCacheDatas = SkeletalMesh->GetImportedModel()->InlineReductionCacheDatas;
		if (!InlineReductionCacheDatas.IsValidIndex(LODIndex))
		{
			InlineReductionCacheDatas.AddDefaulted((LODIndex + 1) - InlineReductionCacheDatas.Num());
		}
		if (ensure(InlineReductionCacheDatas.IsValidIndex(LODIndex)))
		{
			InlineReductionCacheDatas[LODIndex].SetCacheGeometryInfo(BuildLODModel);
		}

		// Re-Apply the user section changes, the UserSectionsData is map to original section and should match the built LODModel
		BuildLODModel.SyncronizeUserSectionsDataArray();

		if (bNaniteBuildEnabled)
		{
			SlowTask.EnterProgressFrame(1.0f, NSLOCTEXT("SkeltalMeshBuilder", "BuildingNaniteData", "Building Nanite data..."));

			FSkeletalMeshRenderData* SkeletalMeshRenderData = SkeletalMesh->GetResourceForRendering();
			check(SkeletalMeshRenderData != nullptr);

			ClearNaniteResources(SkeletalMeshRenderData->NaniteResourcesPtr);

			Nanite::FResources& NaniteResources = *SkeletalMeshRenderData->NaniteResourcesPtr.Get();

			bool bBuildSuccess = BuildNanite(
				SkeletalMesh,
				BuildLODModel,
				SkeletalMeshModel,
				NaniteResources
			);
		}

		// Re-apply the morph target
		SlowTask.EnterProgressFrame(1.0f, NSLOCTEXT("SkeltalMeshBuilder", "RebuildMorphTarget", "Rebuilding morph targets..."));
		if (SkeletalMeshImportData.MorphTargetNames.Num() > 0)
		{
			FLODUtilities::BuildMorphTargets(SkeletalMesh, SkeletalMeshModel, SkeletalMeshImportData, LODIndex, !Options.bComputeNormals, !Options.bComputeTangents, Options.bUseMikkTSpace, Options.OverlappingThresholds);
		}

		// Re-apply the alternate skinning it must be after the inline reduction
		SlowTask.EnterProgressFrame(1.0f, NSLOCTEXT("SkeltalMeshBuilder", "RebuildAlternateSkinning", "Rebuilding alternate skinning..."));
		const TArray<FSkinWeightProfileInfo> SkinProfiles = SkeletalMesh->GetSkinWeightProfiles();
		for (int32 SkinProfileIndex = 0; SkinProfileIndex < SkinProfiles.Num(); ++SkinProfileIndex)
		{
			const FSkinWeightProfileInfo& ProfileInfo = SkinProfiles[SkinProfileIndex];
			FLODUtilities::UpdateAlternateSkinWeights(SkeletalMesh, ProfileInfo.Name, LODIndex, Options);
		}

		// Copy vertex attribute definitions and their values from the import model.
		FLODUtilities::UpdateLODInfoVertexAttributes(SkeletalMesh, LODIndex, LODIndex, /*CopyAttributeValues*/true);
		
		FSkeletalMeshUpdateContext UpdateContext;
		UpdateContext.SkeletalMesh = SkeletalMesh;

		// We are reduce ourself in this case we reduce ourself from the original data and return true.
		if (SkeletalMesh->IsReductionActive(LODIndex))
		{
			SlowTask.EnterProgressFrame(1.0f, NSLOCTEXT("SkeltalMeshBuilder", "RegenerateLOD", "Regenerate LOD..."));
			// Update the original reduction data since we just build a new LODModel.
			if (LODInfo->ReductionSettings.BaseLOD == LODIndex && SkeletalMesh->HasMeshDescription(LODIndex))
			{
				if (LODIndex == 0)
				{
					SkeletalMesh->GetLODInfo(LODIndex)->SourceImportFilename = SkeletalMesh->GetAssetImportData()->GetFirstFilename();
				}
			}
			FLODUtilities::SimplifySkeletalMeshLOD(UpdateContext, LODIndex, SkeletalMeshBuildParameters.TargetPlatform, false);
		}
		else
		{
			if (LODInfo->BonesToRemove.Num() > 0 && SkeletalMesh->GetSkeleton())
			{
				TArray<FName> BonesToRemove;
				BonesToRemove.Reserve(LODInfo->BonesToRemove.Num());
				for (const FBoneReference& BoneReference : LODInfo->BonesToRemove)
				{
					BonesToRemove.Add(BoneReference.BoneName);
				}
				MeshUtilities.RemoveBonesFromMesh(SkeletalMesh, LODIndex, &BonesToRemove);
			}
		}
	}

	FSkeletalMeshLODModel& LODModelAfterReduction = SkeletalMesh->GetImportedModel()->LODModels[LODIndex];
	
	// Re-apply the clothing using the UserSectionsData, this will ensure we remap correctly the cloth if the reduction has change the number of sections
	SlowTask.EnterProgressFrame(1.0f, NSLOCTEXT("SkeltalMeshBuilder", "RebuildClothing", "Rebuilding clothing..."));
	FLODUtilities::RestoreClothingFromBackup(SkeletalMesh, ClothingBindings, LODIndex);

	LODModelAfterReduction.SyncronizeUserSectionsDataArray();
	LODModelAfterReduction.NumTexCoords = NumTexCoord;
	LODModelAfterReduction.BuildStringID = BackupBuildStringID;

	SlowTask.EnterProgressFrame(1.0f, NSLOCTEXT("SkeltalMeshBuilder", "RegenerateDependentLODs", "Regenerate Dependent LODs..."));
	if (SkeletalMeshBuildParameters.bRegenDepLODs)
	{
		// Regenerate dependent LODs
		FLODUtilities::RegenerateDependentLODs(SkeletalMesh, LODIndex, SkeletalMeshBuildParameters.TargetPlatform);
	}

	return true;
}
