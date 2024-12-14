// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/AnimPoseSearchProvider.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNodeBase.h"
#include "Features/IModularFeatures.h"
#include "Modules/ModuleManager.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchLibrary.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "PoseSearchFeatureChannel_PermutationTime.h"
#include "RewindDebuggerRuntimeInterface/IRewindDebuggerRuntimeExtension.h"

class FRewindDebuggerPoseSearchRuntime : public IRewindDebuggerRuntimeExtension 
{
public:
	virtual void RecordingStarted() override
	{
		UE::Trace::ToggleChannel(TEXT("PoseSearch"), true);
	}
	
	virtual void RecordingStopped() override
	{
		UE::Trace::ToggleChannel(TEXT("PoseSearch"), false);
	}
};

class FPoseSearchModule final : public IModuleInterface, public UE::Anim::IPoseSearchProvider
{
public:
	
	// IModuleInterface
	virtual void StartupModule() override
	{
		IModularFeatures::Get().RegisterModularFeature(UE::Anim::IPoseSearchProvider::GetModularFeatureName(), this);
		IModularFeatures::Get().RegisterModularFeature(IRewindDebuggerRuntimeExtension::ModularFeatureName, &RewindDebuggerPoseSearchRuntime);
	}

	virtual void ShutdownModule() override
	{
		IModularFeatures::Get().UnregisterModularFeature(UE::Anim::IPoseSearchProvider::GetModularFeatureName(), this);
		IModularFeatures::Get().UnregisterModularFeature(IRewindDebuggerRuntimeExtension::ModularFeatureName, &RewindDebuggerPoseSearchRuntime);
	}

	// IPoseSearchProvider
	virtual UE::Anim::IPoseSearchProvider::FSearchResult Search(const FAnimationBaseContext& GraphContext, TArrayView<const UObject*> AssetsToSearch,
		const FSearchPlayingAsset& PlayingAsset, const FSearchFutureAsset& FutureAsset) const override
	{
		using namespace UE::PoseSearch;

		FPoseSearchContinuingProperties ContinuingProperties;
		ContinuingProperties.PlayingAsset = PlayingAsset.Asset;
		ContinuingProperties.PlayingAssetAccumulatedTime = PlayingAsset.AccumulatedTime;

		FPoseSearchFutureProperties Future;
		Future.Animation = FutureAsset.Asset;
		Future.AnimationTime = FutureAsset.AccumulatedTime;
		Future.IntervalTime = FutureAsset.IntervalTime;

		const IPoseHistory* PoseHistory = nullptr;
		if (FPoseHistoryProvider* PoseHistoryProvider = GraphContext.GetMessage<FPoseHistoryProvider>())
		{
			PoseHistory = &PoseHistoryProvider->GetPoseHistory();
		}

		UAnimInstance* AnimInstance = Cast<UAnimInstance>(GraphContext.AnimInstanceProxy->GetAnimInstanceObject());
		check(AnimInstance);
		
		const UE::PoseSearch::FSearchResult SearchResult = UPoseSearchLibrary::MotionMatch(MakeArrayView(&AnimInstance, 1),
			MakeArrayView(&DefaultRole, 1), MakeArrayView(&PoseHistory, 1), AssetsToSearch, ContinuingProperties, Future);

		UE::Anim::IPoseSearchProvider::FSearchResult ProviderResult;
		if (const UE::PoseSearch::FSearchIndexAsset* SearchIndexAsset = SearchResult.GetSearchIndexAsset())
		{
			const UPoseSearchDatabase* Database = SearchResult.Database.Get();
			if (const FPoseSearchDatabaseAnimationAssetBase* DatabaseAnimationAssetBase = Database->GetDatabaseAnimationAsset<FPoseSearchDatabaseAnimationAssetBase>(*SearchIndexAsset))
			{
				ProviderResult.SelectedAsset = DatabaseAnimationAssetBase->GetAnimationAsset();
				ProviderResult.Dissimilarity = SearchResult.PoseCost.GetTotalCost();
				ProviderResult.TimeOffsetSeconds = SearchResult.AssetTime;
				ProviderResult.bIsFromContinuingPlaying = SearchResult.bIsContinuingPoseSearch;
				ProviderResult.bMirrored = SearchIndexAsset->IsMirrored();

				// figuring out the WantedPlayRate
				ProviderResult.WantedPlayRate = 1.f;
				if (Future.Animation && Future.IntervalTime > 0.f)
				{
					if (const UPoseSearchFeatureChannel_PermutationTime* PermutationTimeChannel = Database->Schema->FindFirstChannelOfType<UPoseSearchFeatureChannel_PermutationTime>())
					{
						const FSearchIndex& SearchIndex = Database->GetSearchIndex();
						if (!SearchIndex.IsValuesEmpty())
						{
							TConstArrayView<float> ResultData = Database->GetSearchIndex().GetPoseValues(SearchResult.PoseIdx);
							const float ActualIntervalTime = PermutationTimeChannel->GetPermutationTime(ResultData);
							ProviderResult.WantedPlayRate = ActualIntervalTime / Future.IntervalTime;
						}
					}
				}
			}
		}

		return ProviderResult;
	}

private:
	FRewindDebuggerPoseSearchRuntime RewindDebuggerPoseSearchRuntime;
};

IMPLEMENT_MODULE(FPoseSearchModule, PoseSearch);