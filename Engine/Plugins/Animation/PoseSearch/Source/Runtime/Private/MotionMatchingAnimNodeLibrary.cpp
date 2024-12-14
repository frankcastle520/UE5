// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/MotionMatchingAnimNodeLibrary.h"
#include "PoseSearch/AnimNode_MotionMatching.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "AlphaBlend.h"

FMotionMatchingBlueprintBlendSettings::FMotionMatchingBlueprintBlendSettings()
	: BlendTime(0.2f)
	, BlendProfile(nullptr)
	, BlendOption(EAlphaBlendOption::Linear)
	, bUseInertialBlend(false)
{
}

FMotionMatchingAnimNodeReference UMotionMatchingAnimNodeLibrary::ConvertToMotionMatchingNode(const FAnimNodeReference& Node, EAnimNodeReferenceConversionResult& Result)
{
	return FAnimNodeReference::ConvertToType<FMotionMatchingAnimNodeReference>(Node, Result);
}

void UMotionMatchingAnimNodeLibrary::GetMotionMatchingSearchResult(const FMotionMatchingAnimNodeReference& MotionMatchingNode, FPoseSearchBlueprintResult& Result, bool& bIsResultValid)
{
	using namespace UE::PoseSearch;

	bIsResultValid = false;
	if (FAnimNode_MotionMatching* MotionMatchingNodePtr = MotionMatchingNode.GetAnimNodePtr<FAnimNode_MotionMatching>())
	{
		const FMotionMatchingState& MotionMatchingState = MotionMatchingNodePtr->GetMotionMatchingState();
		if (const FSearchIndexAsset* SearchIndexAsset = MotionMatchingState.CurrentSearchResult.GetSearchIndexAsset())
		{
			const UPoseSearchDatabase* CurrentResultDatabase = MotionMatchingState.CurrentSearchResult.Database.Get();
			if (CurrentResultDatabase && CurrentResultDatabase->Schema)
			{
				if (const FPoseSearchDatabaseAnimationAssetBase* DatabaseAsset = CurrentResultDatabase->GetDatabaseAnimationAsset<FPoseSearchDatabaseAnimationAssetBase>(*SearchIndexAsset))
				{
					Result.SelectedAnimation = DatabaseAsset->GetAnimationAsset();
					Result.SelectedTime = MotionMatchingState.CurrentSearchResult.AssetTime;
					Result.bIsContinuingPoseSearch = MotionMatchingState.CurrentSearchResult.bIsContinuingPoseSearch;
					Result.bLoop = SearchIndexAsset->IsLooping();
					Result.bIsMirrored = SearchIndexAsset->IsMirrored();
					Result.BlendParameters = SearchIndexAsset->GetBlendParameters();
					Result.SelectedDatabase = CurrentResultDatabase;
					Result.WantedPlayRate = MotionMatchingState.WantedPlayRate;
					bIsResultValid = true;
				}
			}
		}
	}
	else
	{
		UE_LOG(LogPoseSearch, Warning, TEXT("UMotionMatchingAnimNodeLibrary::SetDatabase called on an invalid context or with an invalid type"));
	}
}

void UMotionMatchingAnimNodeLibrary::GetMotionMatchingBlendSettings(const FMotionMatchingAnimNodeReference& MotionMatchingNode, FMotionMatchingBlueprintBlendSettings& BlendSettings, bool& bIsResultValid)
{
	if (FAnimNode_MotionMatching* MotionMatchingNodePtr = MotionMatchingNode.GetAnimNodePtr<FAnimNode_MotionMatching>())
	{
		BlendSettings.BlendOption = MotionMatchingNodePtr->BlendOption;
		BlendSettings.BlendProfile = MotionMatchingNodePtr->BlendProfile;
		BlendSettings.BlendTime = MotionMatchingNodePtr->BlendTime;
		BlendSettings.bUseInertialBlend = MotionMatchingNodePtr->bUseInertialBlend;
	}
	else
	{
		BlendSettings = FMotionMatchingBlueprintBlendSettings();
		UE_LOG(LogPoseSearch, Warning, TEXT("UMotionMatchingAnimNodeLibrary::GetMotionMatchingBlendSettings called on an invalid context or with an invalid type"));
	}
}

void UMotionMatchingAnimNodeLibrary::OverrideMotionMatchingBlendSettings(const FMotionMatchingAnimNodeReference& MotionMatchingNode, const FMotionMatchingBlueprintBlendSettings& BlendSettings, bool& bIsResultValid)
{
	if (FAnimNode_MotionMatching* MotionMatchingNodePtr = MotionMatchingNode.GetAnimNodePtr<FAnimNode_MotionMatching>())
	{
		 MotionMatchingNodePtr->BlendOption = BlendSettings.BlendOption;
		 MotionMatchingNodePtr->BlendProfile = BlendSettings.BlendProfile;
		 MotionMatchingNodePtr->BlendTime = BlendSettings.BlendTime;
		 MotionMatchingNodePtr->bUseInertialBlend = BlendSettings.bUseInertialBlend;
	}
	else
	{
		UE_LOG(LogPoseSearch, Warning, TEXT("UMotionMatchingAnimNodeLibrary::OverrideMotionMatchingBlendSettings called on an invalid context or with an invalid type"));
	}
}

void UMotionMatchingAnimNodeLibrary::SetDatabaseToSearch(const FMotionMatchingAnimNodeReference& MotionMatchingNode, UPoseSearchDatabase* Database, EPoseSearchInterruptMode InterruptMode)
{
	if (FAnimNode_MotionMatching* MotionMatchingNodePtr = MotionMatchingNode.GetAnimNodePtr<FAnimNode_MotionMatching>())
	{
		MotionMatchingNodePtr->SetDatabaseToSearch(Database, InterruptMode);
	}
	else
	{
		UE_LOG(LogPoseSearch, Warning, TEXT("UMotionMatchingAnimNodeLibrary::SetDatabase called on an invalid context or with an invalid type"));
	}
}

void UMotionMatchingAnimNodeLibrary::SetDatabasesToSearch(const FMotionMatchingAnimNodeReference& MotionMatchingNode, const TArray<UPoseSearchDatabase*>& Databases, EPoseSearchInterruptMode InterruptMode)
{
	if (FAnimNode_MotionMatching* MotionMatchingNodePtr = MotionMatchingNode.GetAnimNodePtr<FAnimNode_MotionMatching>())
	{
		MotionMatchingNodePtr->SetDatabasesToSearch(Databases, InterruptMode);
	}
	else
	{
		UE_LOG(LogPoseSearch, Warning, TEXT("UMotionMatchingAnimNodeLibrary::SetDatabases called on an invalid context or with an invalid type"));
	}
}

void UMotionMatchingAnimNodeLibrary::ResetDatabasesToSearch(const FMotionMatchingAnimNodeReference& MotionMatchingNode, EPoseSearchInterruptMode InterruptMode)
{
	if (FAnimNode_MotionMatching* MotionMatchingNodePtr = MotionMatchingNode.GetAnimNodePtr<FAnimNode_MotionMatching>())
	{
		MotionMatchingNodePtr->ResetDatabasesToSearch(InterruptMode);
	}
	else
	{
		UE_LOG(LogPoseSearch, Warning, TEXT("UMotionMatchingAnimNodeLibrary::ResetDatabasesToSearch called on an invalid context or with an invalid type"));
	}
}

void UMotionMatchingAnimNodeLibrary::SetInterruptMode(const FMotionMatchingAnimNodeReference& MotionMatchingNode, EPoseSearchInterruptMode InterruptMode)
{
	if (FAnimNode_MotionMatching* MotionMatchingNodePtr = MotionMatchingNode.GetAnimNodePtr<FAnimNode_MotionMatching>())
	{
		MotionMatchingNodePtr->SetInterruptMode(InterruptMode);
	}
	else
	{
		UE_LOG(LogPoseSearch, Warning, TEXT("UMotionMatchingAnimNodeLibrary::ForceInterruptOneFrame called on an invalid context or with an invalid type"));
	}
}