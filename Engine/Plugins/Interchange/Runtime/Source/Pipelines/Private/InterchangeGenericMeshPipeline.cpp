// Copyright Epic Games, Inc. All Rights Reserved. 
#include "InterchangeGenericMeshPipeline.h"

#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "InterchangeAnimationTrackSetNode.h"
#include "InterchangeGenericAssetsPipeline.h"
#include "InterchangeMeshNode.h"
#include "InterchangePipelineLog.h"
#include "InterchangePipelineMeshesUtilities.h"
#include "InterchangeSceneNode.h"
#include "InterchangeSkeletalMeshFactoryNode.h"
#include "InterchangeStaticMeshFactoryNode.h"
#include "InterchangeSourceData.h"
#include "Misc/Paths.h"
#include "Nodes/InterchangeBaseNode.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "InterchangeCustomVersion.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(InterchangeGenericMeshPipeline)

FString UInterchangeGenericMeshPipeline::GetPipelineCategory(UClass* AssetClass)
{
	if (ensure(AssetClass))
	{
		if (AssetClass->IsChildOf(UStaticMesh::StaticClass()))
		{
			return TEXT("Static Meshes");
		}
		else if (AssetClass->IsChildOf(USkeletalMesh::StaticClass()))
		{
			return TEXT("Skeletal Meshes");
		}
	}
	return TEXT("Static Meshes");
}

void UInterchangeGenericMeshPipeline::AdjustSettingsForContext(const FInterchangePipelineContextParams& ContextParams)
{
	Super::AdjustSettingsForContext(ContextParams);

#if WITH_EDITOR

	check(CommonSkeletalMeshesAndAnimationsProperties.IsValid());
	if (ContextParams.ContextType == EInterchangePipelineContext::None)
	{
		//We do not change the setting if we are in editing context
		return;
	}

	bool bAutoDetectConvertStaticMeshToSkeletalMesh = false;
	bool bContainStaticMesh = false;
	bool bContainSkeletalMesh = false;
	bool bContainStaticMeshAnimationNode = false;
	bool bIgnoreStaticMesh = false;
	GetMeshesInformationFromTranslatedData(ContextParams.BaseNodeContainer, bAutoDetectConvertStaticMeshToSkeletalMesh, bContainStaticMesh, bContainSkeletalMesh, bContainStaticMeshAnimationNode, bIgnoreStaticMesh);

	//Avoid creating physics asset when importing a LOD or the alternate skinning
	if (ContextParams.ContextType == EInterchangePipelineContext::AssetCustomLODImport
		|| ContextParams.ContextType == EInterchangePipelineContext::AssetCustomLODReimport
		|| ContextParams.ContextType == EInterchangePipelineContext::AssetAlternateSkinningImport
		|| ContextParams.ContextType == EInterchangePipelineContext::AssetAlternateSkinningReimport
		|| ContextParams.ContextType == EInterchangePipelineContext::AssetCustomMorphTargetImport
		|| ContextParams.ContextType == EInterchangePipelineContext::AssetCustomMorphTargetReImport)
	{
		bCreatePhysicsAsset = false;
		PhysicsAsset = nullptr;
		LodGroup = NAME_None;
		
		if (ContextParams.ContextType == EInterchangePipelineContext::AssetAlternateSkinningImport
			|| ContextParams.ContextType == EInterchangePipelineContext::AssetAlternateSkinningReimport)
		{
			CommonMeshesProperties->ForceAllMeshAsType = EInterchangeForceMeshType::IFMT_SkeletalMesh;
			CommonMeshesProperties->bAutoDetectMeshType = false;
			CommonMeshesProperties->bBakeMeshes = true;
			CommonMeshesProperties->bBakePivotMeshes = false;
			CommonMeshesProperties->bImportLods = false;
			CommonMeshesProperties->bKeepSectionsSeparate = false;
			CommonMeshesProperties->VertexColorImportOption = EInterchangeVertexColorImportOption::IVCIO_Ignore;
			bImportSkeletalMeshes = true;
			bImportStaticMeshes = false;
			bBuildNanite = false;
			bImportMorphTargets = false;
			bImportVertexAttributes = false;
			bUpdateSkeletonReferencePose = false;
			SkeletalMeshImportContentType = EInterchangeSkeletalMeshContentType::All;
			CommonSkeletalMeshesAndAnimationsProperties->Skeleton = nullptr;
			CommonSkeletalMeshesAndAnimationsProperties->bImportOnlyAnimations = false;
		}
		else if (ContextParams.ContextType == EInterchangePipelineContext::AssetCustomMorphTargetImport
			|| ContextParams.ContextType == EInterchangePipelineContext::AssetCustomMorphTargetReImport)
		{
			//Custom morph target are imported has a combined static mesh
			CommonMeshesProperties->ForceAllMeshAsType = EInterchangeForceMeshType::IFMT_StaticMesh;
			CommonMeshesProperties->bAutoDetectMeshType = false;
			CommonMeshesProperties->bBakeMeshes = true;
			CommonMeshesProperties->bBakePivotMeshes = false;
			CommonMeshesProperties->bImportLods = true;
			CommonMeshesProperties->bKeepSectionsSeparate = false;
			CommonMeshesProperties->VertexColorImportOption = EInterchangeVertexColorImportOption::IVCIO_Ignore;
			bImportSkeletalMeshes = false;
			bImportStaticMeshes = true;
			bCombineStaticMeshes = true;
			bBuildNanite = false;
			LodGroup = NAME_None;
			bCollision = false;
			Collision = EInterchangeMeshCollision::None;
			bImportCollisionAccordingToMeshName = false;
			bGenerateLightmapUVs = false;
			bGenerateDistanceFieldAsIfTwoSided = false;
			bSupportFaceRemap = false;
		}
		else if (ContextParams.ContextType == EInterchangePipelineContext::AssetCustomLODImport
			|| ContextParams.ContextType == EInterchangePipelineContext::AssetCustomLODReimport)
		{
			//We are importing custom LODs
			if (ContextParams.ImportObjectType)
			{
				//If we have a provided import object type we can make sure we import the correct type
				if (ContextParams.ImportObjectType->IsChildOf<UStaticMesh>())
				{
					bImportStaticMeshes = true;
					CommonMeshesProperties->ForceAllMeshAsType = EInterchangeForceMeshType::IFMT_StaticMesh;
					CommonMeshesProperties->bAutoDetectMeshType = false;
					bImportSkeletalMeshes = false;
					bCombineStaticMeshes = true;
					LodGroup = NAME_None;
					bSupportFaceRemap = false;
					bCollision = false;
					Collision = EInterchangeMeshCollision::None;
					bImportCollisionAccordingToMeshName = false;
					bGenerateLightmapUVs = false;
					bGenerateDistanceFieldAsIfTwoSided = false;
				}
				else if (ContextParams.ImportObjectType->IsChildOf<USkeletalMesh>())
				{
					bImportSkeletalMeshes = true;
					CommonMeshesProperties->ForceAllMeshAsType = EInterchangeForceMeshType::IFMT_SkeletalMesh;
					CommonMeshesProperties->bAutoDetectMeshType = false;
					bCreatePhysicsAsset = false;
					bImportStaticMeshes = false;
				}
				else
				{
					CommonMeshesProperties->bAutoDetectMeshType = true;
				}
			}
		}
	}
	const FString CommonMeshesCategory = UInterchangeGenericCommonMeshesProperties::GetPipelineCategory(nullptr);
	const FString StaticMeshesCategory = UInterchangeGenericMeshPipeline::GetPipelineCategory(UStaticMesh::StaticClass());
	const FString SkeletalMeshesCategory = UInterchangeGenericMeshPipeline::GetPipelineCategory(USkeletalMesh::StaticClass());
	const FString CommonSkeletalMeshesAndAnimationCategory = UInterchangeGenericCommonSkeletalMeshesAndAnimationsProperties::GetPipelineCategory(nullptr);

	TArray<FString> HideCategories;
	TArray<FString> HideSubCategories;
	if (ContextParams.ContextType == EInterchangePipelineContext::AssetReimport)
	{
		CommonMeshesProperties->bAutoDetectMeshType = false;

		HideSubCategories.Add(TEXT("Build"));
		if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(ContextParams.ReimportAsset))
		{
			//Set the skeleton to the current asset skeleton
			CommonSkeletalMeshesAndAnimationsProperties->Skeleton = SkeletalMesh->GetSkeleton();
			PhysicsAsset = SkeletalMesh->GetPhysicsAsset();
			if (PhysicsAsset.IsValid())
			{
				bCreatePhysicsAsset = false;
			}
			bImportStaticMeshes = false;
			HideCategories.Add(StaticMeshesCategory);
			if(!bContainSkeletalMesh
				|| SkeletalMeshImportContentType == EInterchangeSkeletalMeshContentType::Geometry
				|| CommonMeshesProperties->ForceAllMeshAsType == EInterchangeForceMeshType::IFMT_StaticMesh)
			{
				CommonMeshesProperties->ForceAllMeshAsType = EInterchangeForceMeshType::IFMT_SkeletalMesh;
			}
		}
		else if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(ContextParams.ReimportAsset))
		{
			HideCategories.Add(SkeletalMeshesCategory);
			HideCategories.Add(CommonSkeletalMeshesAndAnimationCategory);
			bImportSkeletalMeshes = false;
			if (!bContainStaticMesh
				|| CommonMeshesProperties->ForceAllMeshAsType == EInterchangeForceMeshType::IFMT_SkeletalMesh)
			{
				CommonMeshesProperties->ForceAllMeshAsType = EInterchangeForceMeshType::IFMT_StaticMesh;
			}
		}
		else if (UAnimSequence* AnimSequence = Cast<UAnimSequence>(ContextParams.ReimportAsset))
		{
			HideCategories.Add(StaticMeshesCategory);
			HideCategories.Add(SkeletalMeshesCategory);
			HideCategories.Add(CommonMeshesCategory);
		}
		else if (ContextParams.ReimportAsset)
		{
			HideCategories.Add(StaticMeshesCategory);
			HideCategories.Add(SkeletalMeshesCategory);
			HideCategories.Add(CommonMeshesCategory);
			HideCategories.Add(CommonSkeletalMeshesAndAnimationCategory);
		}
	}

	if (UInterchangePipelineBase* OuterMostPipeline = GetMostPipelineOuter())
	{
		constexpr bool bDoTransientSubPipeline = true;
		if (UInterchangeGenericAssetsPipeline* ParentPipeline = Cast<UInterchangeGenericAssetsPipeline>(OuterMostPipeline))
		{
			if (ParentPipeline->ReimportStrategy == EReimportStrategyFlags::ApplyNoProperties)
			{
				for (const FString& HideSubCategoryName : HideSubCategories)
				{
					HidePropertiesOfSubCategory(OuterMostPipeline, this, HideSubCategoryName, bDoTransientSubPipeline);
				}
			}
		}

		for (const FString& HideCategoryName : HideCategories)
		{
			HidePropertiesOfCategory(OuterMostPipeline, this, HideCategoryName, bDoTransientSubPipeline);
		}
	}
#endif //WITH_EDITOR
}

#if WITH_EDITOR

bool UInterchangeGenericMeshPipeline::IsPropertyChangeNeedRefresh(const FPropertyChangedEvent& PropertyChangedEvent) const
{
	static const TSet<FName> NeedRefreshProperties =
	{
		GET_MEMBER_NAME_CHECKED(UInterchangeGenericMeshPipeline, SkeletalMeshImportContentType),
		GET_MEMBER_NAME_CHECKED(UInterchangeGenericMeshPipeline, bImportStaticMeshes),
		GET_MEMBER_NAME_CHECKED(UInterchangeGenericMeshPipeline, bImportSkeletalMeshes),
		GET_MEMBER_NAME_CHECKED(UInterchangeGenericMeshPipeline, bCreatePhysicsAsset),
		GET_MEMBER_NAME_CHECKED(UInterchangeGenericMeshPipeline, bCombineStaticMeshes)
	};
	
	if (NeedRefreshProperties.Contains(PropertyChangedEvent.GetPropertyName()))
	{
		return true;
	}
	return Super::IsPropertyChangeNeedRefresh(PropertyChangedEvent);
}

#endif //WITH_EDITOR

void UInterchangeGenericMeshPipeline::PreDialogCleanup(const FName PipelineStackName)
{
	//Do not change the physics asset if this pipeline is a re-import or an override pipeline
	if (!IsFromReimportOrOverride())
	{
		PhysicsAsset = nullptr;
	}
}

#if WITH_EDITOR

bool UInterchangeGenericMeshPipeline::GetPropertyPossibleValues(const FName PropertyPath, TArray<FString>& PossibleValues)
{
	FString PropertyPathString = PropertyPath.ToString();
	int32 PropertyNameIndex = INDEX_NONE;
	if (PropertyPathString.FindLastChar(':', PropertyNameIndex))
	{
		PropertyPathString = PropertyPathString.RightChop(PropertyNameIndex+1);
	}
	if (PropertyPathString.Equals(GET_MEMBER_NAME_STRING_CHECKED(UInterchangeGenericMeshPipeline, LodGroup)))
	{
		TArray<FName> LODGroupNames;
		UStaticMesh::GetLODGroups(LODGroupNames);
		for (int32 GroupIndex = 0; GroupIndex < LODGroupNames.Num(); ++GroupIndex)
		{
			PossibleValues.Add(LODGroupNames[GroupIndex].GetPlainNameString());
		}
		return true;
	}
	//If we did not find any property call the super implementation
	return Super::GetPropertyPossibleValues(PropertyPath, PossibleValues);
}

void UInterchangeGenericMeshPipeline::GetSupportAssetClasses(TArray<UClass*>& PipelineSupportAssetClasses) const
{
	PipelineSupportAssetClasses.Add(UStaticMesh::StaticClass());
	PipelineSupportAssetClasses.Add(USkeletalMesh::StaticClass());
	if (bCreatePhysicsAsset && !PhysicsAsset.IsValid())
	{
		PipelineSupportAssetClasses.Add(UPhysicsAsset::StaticClass());
	}
}

#endif

void UInterchangeGenericMeshPipeline::GetMeshesInformationFromTranslatedData(const UInterchangeBaseNodeContainer* InBaseNodeContainer
	, bool& bAutoDetectConvertStaticMeshToSkeletalMesh
	, bool& bContainStaticMesh
	, bool& bContainSkeletalMesh
	, bool& bContainStaticMeshAnimationNode
	, bool& bIgnoreStaticMeshes) const
{
	//Its valid to call GetMeshesInformationFromTranslatedData with a null container
	if (!InBaseNodeContainer)
	{
		return;
	}
	bAutoDetectConvertStaticMeshToSkeletalMesh = false;
	bContainStaticMesh = false;
	bContainSkeletalMesh = false;
	bContainStaticMeshAnimationNode = false;
	bIgnoreStaticMeshes = false;
	{
		TArray<FString> StaticMeshNodeUids;
		InBaseNodeContainer->IterateNodesOfType<UInterchangeMeshNode>([&bContainSkeletalMesh, &StaticMeshNodeUids](const FString& NodeUid, UInterchangeMeshNode* MeshNode)
			{
				if (!MeshNode->IsMorphTarget())
				{
					MeshNode->IsSkinnedMesh() ? bContainSkeletalMesh = true : StaticMeshNodeUids.Add(NodeUid);
				}
			});
		bContainStaticMesh = !StaticMeshNodeUids.IsEmpty();

		TMap<const UInterchangeSceneNode*, bool> CacheProcessSceneNodes;
		InBaseNodeContainer->BreakableIterateNodesOfType<UInterchangeTransformAnimationTrackNode>([&InBaseNodeContainer, &bContainStaticMeshAnimationNode, &StaticMeshNodeUids, &CacheProcessSceneNodes](const FString& NodeUid, UInterchangeTransformAnimationTrackNode* AnimationNode)
			{
				FString SceneNodeUid;
				if (AnimationNode->GetCustomActorDependencyUid(SceneNodeUid))
				{
					if (const UInterchangeSceneNode* SceneNode = Cast<UInterchangeSceneNode>(InBaseNodeContainer->GetNode(SceneNodeUid)))
					{
						if (IsImpactingAnyMeshesRecursive(SceneNode, InBaseNodeContainer, StaticMeshNodeUids, CacheProcessSceneNodes))
						{
							bContainStaticMeshAnimationNode = true;
						}
					}
				}
				return bContainStaticMeshAnimationNode;
			});
	}

	if (CommonMeshesProperties->bAutoDetectMeshType && CommonMeshesProperties->ForceAllMeshAsType == EInterchangeForceMeshType::IFMT_None)
	{
		if (!bContainSkeletalMesh && bContainStaticMesh)
		{
			//Auto detect some static mesh transform animations, we need to force the skeletal mesh type and recompute
			bAutoDetectConvertStaticMeshToSkeletalMesh = bContainStaticMeshAnimationNode;
		}
		else if (bContainSkeletalMesh)
		{
			bIgnoreStaticMeshes = true;
		}
	}
}

void UInterchangeGenericMeshPipeline::PostLoad()
{
	Super::PostLoad();
	if(!bImportCollision_DEPRECATED)
	{
		bCollision = bImportCollision_DEPRECATED;
	}
}

UInterchangePipelineMeshesUtilities* UInterchangeGenericMeshPipeline::CreateMeshPipelineUtilities(UInterchangeBaseNodeContainer* InBaseNodeContainer
	, const UInterchangeGenericMeshPipeline* Pipeline)
{
	UInterchangePipelineMeshesUtilities* CreatedPipelineMeshesUtilities = UInterchangePipelineMeshesUtilities::CreateInterchangePipelineMeshesUtilities(InBaseNodeContainer);

	bool bAutoDetectConvertStaticMeshToSkeletalMesh = false;
	bool bContainStaticMesh = false;
	bool bContainSkeletalMesh = false;
	bool bContainStaticMeshAnimationNode = false;
	bool bIgnoreStaticMeshes = false;
	Pipeline->GetMeshesInformationFromTranslatedData(InBaseNodeContainer, bAutoDetectConvertStaticMeshToSkeletalMesh, bContainStaticMesh, bContainSkeletalMesh, bContainStaticMeshAnimationNode, bIgnoreStaticMeshes);

	//Set the context option to use when querying the pipeline mesh utilities
	FInterchangePipelineMeshesUtilitiesContext DataContext;
	
	//We convert to skeletal mesh, only if the translated data do not have skeletal mesh
	//Rigid mesh import is a fallback when there is no skinned mesh
	DataContext.bConvertStaticMeshToSkeletalMesh = !bContainSkeletalMesh && (bAutoDetectConvertStaticMeshToSkeletalMesh || (Pipeline->CommonMeshesProperties->ForceAllMeshAsType == EInterchangeForceMeshType::IFMT_SkeletalMesh));

	//Force static mesh convert all mesh to static mesh
	DataContext.bConvertSkeletalMeshToStaticMesh = (Pipeline->CommonMeshesProperties->ForceAllMeshAsType == EInterchangeForceMeshType::IFMT_StaticMesh);

	DataContext.bConvertStaticsWithMorphTargetsToSkeletals = Pipeline->CommonSkeletalMeshesAndAnimationsProperties->bConvertStaticsWithMorphTargetsToSkeletals;
	DataContext.bImportMeshesInBoneHierarchy = Pipeline->CommonSkeletalMeshesAndAnimationsProperties->bImportMeshesInBoneHierarchy;
	DataContext.bQueryGeometryOnlyIfNoInstance = Pipeline->CommonMeshesProperties->bBakeMeshes || Pipeline->CommonMeshesProperties->bBakePivotMeshes;
	DataContext.bIgnoreStaticMeshes = bIgnoreStaticMeshes;
	CreatedPipelineMeshesUtilities->SetContext(DataContext);
	return CreatedPipelineMeshesUtilities;
}

void UInterchangeGenericMeshPipeline::ExecutePipeline(UInterchangeBaseNodeContainer* InBaseNodeContainer, const TArray<UInterchangeSourceData*>& InSourceDatas, const FString& ContentBasePath)
{
	if (!InBaseNodeContainer)
	{
		UE_LOG(LogInterchangePipeline, Warning, TEXT("UInterchangeGenericMeshPipeline: Cannot execute pre-import pipeline because InBaseNodeContrainer is null."));
		return;
	}
	
	BaseNodeContainer = InBaseNodeContainer;
	SourceDatas.Empty(InSourceDatas.Num());
	for (const UInterchangeSourceData* SourceData : InSourceDatas)
	{
		SourceDatas.Add(SourceData);
	}
	PipelineMeshesUtilities = CreateMeshPipelineUtilities(BaseNodeContainer, this);

	//Create skeletalmesh factory nodes
	ExecutePreImportPipelineSkeletalMesh();

	//Create staticmesh factory nodes
	ExecutePreImportPipelineStaticMesh();
}

void UInterchangeGenericMeshPipeline::ExecutePostImportPipeline(const UInterchangeBaseNodeContainer* InBaseNodeContainer, const FString& FactoryNodeKey, UObject* CreatedAsset, bool bIsAReimport)
{
	//We do not use the provided base container since ExecutePreImportPipeline cache it
	//We just make sure the same one is pass in parameter
	if (!InBaseNodeContainer || !ensure(BaseNodeContainer == InBaseNodeContainer) || !CreatedAsset)
	{
		return;
	}

	const UInterchangeFactoryBaseNode* FactoryNode = BaseNodeContainer->GetFactoryNode(FactoryNodeKey);
	if (!FactoryNode)
	{
		return;
	}

	//Set the last content type import
	LastSkeletalMeshImportContentType = SkeletalMeshImportContentType;

	PostImportSkeletalMesh(CreatedAsset, FactoryNode);

	//Finish the physics asset import, it need the skeletal mesh render data to create the physics collision geometry
	PostImportPhysicsAssetImport(CreatedAsset, FactoryNode);
}

void UInterchangeGenericMeshPipeline::SetReimportSourceIndex(UClass* ReimportObjectClass, const int32 SourceFileIndex)
{
	if (ReimportObjectClass == USkeletalMesh::StaticClass())
	{
		switch (SourceFileIndex)
		{
			case 0:
			{
				//Geo and skinning
				SkeletalMeshImportContentType = EInterchangeSkeletalMeshContentType::All;
			}
			break;

			case 1:
			{
				//Geo only
				SkeletalMeshImportContentType = EInterchangeSkeletalMeshContentType::Geometry;
			}
			break;

			case 2:
			{
				//Skinning only
				SkeletalMeshImportContentType = EInterchangeSkeletalMeshContentType::SkinningWeights;
			}
			break;

			default:
			{
				//In case SourceFileIndex == INDEX_NONE //No specified options, we use the last imported content type
				SkeletalMeshImportContentType = LastSkeletalMeshImportContentType;
			}
		};
	}
}

#if WITH_EDITOR
bool UInterchangeGenericMeshPipeline::DoClassesIncludeAllEditableStructProperties(const TArray<const UClass*>& Classes, const UStruct* Struct)
{
	check(IsInGameThread());

	bool bResult = true;
	const FName CategoryKey("Category");
	for (const FProperty* Property = Struct->PropertyLink; Property; Property = Property->PropertyLinkNext)
	{
		//skip (transient, deprecated, const) property
		if (Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated | CPF_EditConst))
		{
			continue;
		}
		//skip property that is not editable
		if (!Property->HasAnyPropertyFlags(CPF_Edit))
		{
			continue;
		}
		const FObjectProperty* SubObject = CastField<FObjectProperty>(Property);
		if (SubObject)
		{
			continue;
		}
		else if (const FString* PropertyCategoryString = Property->FindMetaData(CategoryKey))
		{
			FName PropertyName = Property->GetFName();
			bool bFindProperty = false;
			for (const UClass* Class : Classes)
			{
				if (Class->FindPropertyByName(PropertyName) != nullptr)
				{
					bFindProperty = true;
					break;
				}
			}
			//Ensure to notify
			if (!bFindProperty)
			{
				UE_LOG(LogInterchangePipeline, Log, TEXT("The Interchange mesh pipeline does not include build property %s."), *PropertyName.ToString());
				bResult = false;
			}
		}
	}
	return bResult;
}
#endif

bool UInterchangeGenericMeshPipeline::IsImpactingAnyMeshesRecursive(const UInterchangeSceneNode* SceneNode
	, const UInterchangeBaseNodeContainer* InBaseNodeContainer
	, const TArray<FString>& StaticMeshNodeUids
	, TMap<const UInterchangeSceneNode*, bool>& CacheProcessSceneNodes)
{
	bool& bIsImpactingCache = CacheProcessSceneNodes.FindOrAdd(SceneNode, false);
	if (bIsImpactingCache)
	{
		return bIsImpactingCache;
	}
	FString AssetUid;
	if (SceneNode->GetCustomAssetInstanceUid(AssetUid))
	{
		if (StaticMeshNodeUids.Contains(AssetUid))
		{
			bIsImpactingCache = true;
			return true;
		}
	}
	TArray<FString> Children = InBaseNodeContainer->GetNodeChildrenUids(SceneNode->GetUniqueID());
	for (const FString& ChildUid : Children)
	{
		if (const UInterchangeSceneNode* ChildSceneNode = Cast<UInterchangeSceneNode>(InBaseNodeContainer->GetNode(ChildUid)))
		{
			if (IsImpactingAnyMeshesRecursive(ChildSceneNode, InBaseNodeContainer, StaticMeshNodeUids, CacheProcessSceneNodes))
			{
				return true;
			}
		}
	}
	return false;
}