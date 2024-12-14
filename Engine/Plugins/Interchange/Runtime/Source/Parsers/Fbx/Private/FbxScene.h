// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FbxInclude.h"

/** Forward declarations */
namespace UE::Interchange::Private
{
	class FPayloadContextBase;
}
class UInterchangeBaseNodeContainer;
class UInterchangeSceneNode;
class UInterchangeSkeletalAnimationTrackNode;

namespace UE
{
	namespace Interchange
	{
		namespace Private
		{
			class FFbxParser;
			struct FMorphTargetAnimationBuildingData;

			class FFbxScene
			{
			public:
				explicit FFbxScene(FFbxParser& InParser)
					: Parser(InParser)
				{}

				void AddHierarchy(FbxScene* SDKScene, UInterchangeBaseNodeContainer& NodeContainer, TMap<FString, TSharedPtr<FPayloadContextBase, ESPMode::ThreadSafe>>& PayloadContexts);
				void AddAnimation(FbxScene* SDKScene, UInterchangeBaseNodeContainer& NodeContainer, TMap<FString, TSharedPtr<FPayloadContextBase, ESPMode::ThreadSafe>>& PayloadContexts);
				void AddMorphTargetAnimations(FbxScene* SDKScene, UInterchangeBaseNodeContainer& NodeContainer, TMap<FString, TSharedPtr<FPayloadContextBase, ESPMode::ThreadSafe>>& PayloadContexts, const TArray<FMorphTargetAnimationBuildingData>& MorphTargetAnimationsBuildingData);
				UInterchangeSceneNode* CreateTransformNode(UInterchangeBaseNodeContainer& NodeContainer, const FString& NodeName, const FString& NodeUniqueID);

				struct FRootJointInfo
				{
					bool bValidBindPose = false;
				};

			protected:
				void CreateMeshNodeReference(UInterchangeSceneNode* UnrealSceneNode, FbxNodeAttribute* NodeAttribute, UInterchangeBaseNodeContainer& NodeContainer, const FTransform& GeometricTransform, const FTransform& PivotNodeTransform);
				void CreateCameraNodeReference(UInterchangeSceneNode* UnrealSceneNode, FbxNodeAttribute* NodeAttribute, UInterchangeBaseNodeContainer& NodeContainer);
				void CreateLightNodeReference(UInterchangeSceneNode* UnrealSceneNode, FbxNodeAttribute* NodeAttribute, UInterchangeBaseNodeContainer& NodeContainer);
				void AddHierarchyRecursively(UInterchangeSceneNode* UnrealParentNode
					, FbxNode* Node
					, FbxScene* SDKScene
					, UInterchangeBaseNodeContainer& NodeContainer
					, TMap<FString, TSharedPtr<FPayloadContextBase, ESPMode::ThreadSafe>>& PayloadContexts
					, TArray<FbxNode*>& ForceJointNodes
					, bool& bBadBindPoseMessageDisplay);

				void AddAnimationRecursively(FbxNode* Node
					, FbxScene* SDKScene
					, UInterchangeBaseNodeContainer& NodeContainer
					, TMap<FString, TSharedPtr<FPayloadContextBase, ESPMode::ThreadSafe>>& PayloadContexts
					, UInterchangeSkeletalAnimationTrackNode* SkeletalAnimationTrackNode, bool SkeletalAnimationAddedToContainer
					, const FString& RootSceneNodeUid, const TSet<FString>& SkeletonRootNodeUids
					, const int32& AnimationIndex
					, TArray<FbxNode*>& ForceJointNodes);

			private:
				void AddRigidAnimation(FbxNode* Node
					, UInterchangeSceneNode* UnrealNode
					, UInterchangeBaseNodeContainer& NodeContainer
					, TMap<FString, TSharedPtr<FPayloadContextBase, ESPMode::ThreadSafe>>& PayloadContexts);

				FbxNode* Internal_GetRootSkeleton(FbxScene* SDKScene, FbxNode* Link);
				void FindCommonJointRootNode(FbxScene* SDKScene, const TArray<FbxNode*>& ForceJointNodes);

				void FindForceJointNode(FbxScene* SDKScene, TArray<FbxNode*>& ForceJointNodes);

				bool IsValidBindPose(FbxScene* SDKScene, FbxNode* RootJoint) const;

				TMap<FbxNode*, FRootJointInfo> CommonJointRootNodes;

				FFbxParser& Parser;
			};
		}//ns Private
	}//ns Interchange
}//ns UE
