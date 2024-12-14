// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/PoseSearchResult.h"
#include "Animation/BlendSpace.h"
#include "StructUtils/InstancedStruct.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchSchema.h"

namespace UE::PoseSearch
{

void FSearchResult::Update(float NewAssetTime)
{
	if (IsValid())
	{
		float RealTime = NewAssetTime;
		const FSearchIndexAsset& SearchIndexAsset = Database->GetSearchIndex().GetAssetForPose(PoseIdx);
		const FPoseSearchDatabaseAnimationAssetBase* DatabaseAnimationAssetBase = Database->GetDatabaseAnimationAsset<FPoseSearchDatabaseAnimationAssetBase>(SearchIndexAsset);
		check(DatabaseAnimationAssetBase);
		if (const UBlendSpace* BlendSpace = Cast<UBlendSpace>(DatabaseAnimationAssetBase->GetAnimationAsset()))
		{
			TArray<FBlendSampleData> BlendSamples;
			int32 TriangulationIndex = 0;
			BlendSpace->GetSamplesFromBlendInput(SearchIndexAsset.GetBlendParameters(), BlendSamples, TriangulationIndex, true);
			const float PlayLength = BlendSpace->GetAnimationLengthFromSampleData(BlendSamples);

			// Asset player time for blendspaces is normalized [0, 1] so we need to convert 
			// to a real time before we advance it
			check(NewAssetTime >= 0.f && NewAssetTime <= 1.f);
			RealTime = NewAssetTime * PlayLength;
		}
		
		PoseIdx = Database->GetPoseIndexFromTime(RealTime, SearchIndexAsset);
		AssetTime = NewAssetTime;
	}
	else
	{
		Reset();
	}
}

const FSearchIndexAsset* FSearchResult::GetSearchIndexAsset(bool bMandatory) const
{
	if (bMandatory)
	{
		check(IsValid());
	}
	else if (!IsValid())
	{
		return nullptr;
	}

	return &Database->GetSearchIndex().GetAssetForPose(PoseIdx);
}

bool FSearchResult::CanAdvance(float DeltaTime) const
{
	bool bCanAdvance = false;
	if (const FSearchIndexAsset* SearchIndexAsset = GetSearchIndexAsset())
	{
		const FPoseSearchDatabaseAnimationAssetBase* DatabaseAnimationAssetBase = Database->GetDatabaseAnimationAsset<FPoseSearchDatabaseAnimationAssetBase>(*SearchIndexAsset);
		check(DatabaseAnimationAssetBase);

		float SteppedTime = AssetTime;
		if (const UBlendSpace* BlendSpace = Cast<UBlendSpace>(DatabaseAnimationAssetBase->GetAnimationAsset()))
		{
			TArray<FBlendSampleData> BlendSamples;
			int32 TriangulationIndex = 0;
			BlendSpace->GetSamplesFromBlendInput(SearchIndexAsset->GetBlendParameters(), BlendSamples, TriangulationIndex, true);

			const float PlayLength = BlendSpace->GetAnimationLengthFromSampleData(BlendSamples);

			// Asset player time for blend spaces is normalized [0, 1] so we need to convert it back to real time before we advance it
			SteppedTime = AssetTime * PlayLength;
			bCanAdvance = ETAA_Finished != FAnimationRuntime::AdvanceTime(SearchIndexAsset->IsLooping(), DeltaTime, SteppedTime, PlayLength);
		}
		else
		{
			const float AssetLength = DatabaseAnimationAssetBase->GetPlayLength();
			bCanAdvance = ETAA_Finished != FAnimationRuntime::AdvanceTime(SearchIndexAsset->IsLooping(), DeltaTime, SteppedTime, AssetLength);
		}
	}
	return bCanAdvance;
}

} // namespace UE::PoseSearch
