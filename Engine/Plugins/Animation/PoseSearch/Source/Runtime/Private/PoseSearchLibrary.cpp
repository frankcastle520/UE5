// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/PoseSearchLibrary.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimNode_Inertialization.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "Animation/AnimRootMotionProvider.h"
#include "Animation/AnimComposite.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSubsystem_Tag.h"
#include "Animation/BlendSpace.h"
#include "Animation/BuiltInAttributeTypes.h"
#include "Animation/AnimTrace.h"
#include "GameFramework/Character.h"
#include "StructUtils/InstancedStruct.h"
#include "PoseSearch/AnimNode_MotionMatching.h"
#include "PoseSearch/AnimNode_PoseSearchHistoryCollector.h"
#include "PoseSearch/MultiAnimAsset.h"
#include "PoseSearch/PoseSearchAnimNotifies.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchDerivedData.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "PoseSearchFeatureChannel_Trajectory.h"
#include "PoseSearch/Trace/PoseSearchTraceLogger.h"
#include "PoseSearchFeatureChannel_PermutationTime.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PoseSearchLibrary)

#define LOCTEXT_NAMESPACE "PoseSearchLibrary"

#if ENABLE_DRAW_DEBUG && ENABLE_ANIM_DEBUG
TAutoConsoleVariable<bool> CVarAnimMotionMatchDrawQueryEnable(TEXT("a.MotionMatch.DrawQuery.Enable"), false, TEXT("Enable / Disable MotionMatch Draw Query"));
TAutoConsoleVariable<bool> CVarAnimMotionMatchDrawMatchEnable(TEXT("a.MotionMatch.DrawMatch.Enable"), false, TEXT("Enable / Disable MotionMatch Draw Match"));
#endif

namespace UE::PoseSearch
{
	// budgeting some stack allocations for simple use cases. bigger requests of AnimationAssets containing UAnimNotifyState_PoseSearchBranchIn 
	// referencing multiple databases will default to a slower TMemStackAllocator (that hides heap allocations)
	enum { MAX_STACK_ALLOCATED_ANIMATIONS = 16 };
	enum { MAX_STACK_ALLOCATED_SETS = 2 };
	typedef	TArray<const UObject*, TInlineAllocator<MAX_STACK_ALLOCATED_ANIMATIONS, TMemStackAllocator<>>> TAssetsToSearch;
	// an empty TAssetsToSearch associated to Database means we need to search ALL the assets
	typedef TMap<const UPoseSearchDatabase*, TAssetsToSearch, TInlineSetAllocator<MAX_STACK_ALLOCATED_SETS, TMemStackSetAllocator<>>> TAssetsToSearchPerDatabaseMap;
	typedef TPair<const UPoseSearchDatabase*, TAssetsToSearch> TAssetsToSearchPerDatabasePair;

	// this function adds AssetToSearch to the search of Database
	// returns bAsyncBuildIndexInProgress
	static bool AddToSearchForDatabase(TAssetsToSearchPerDatabaseMap& AssetsToSearchPerDatabaseMap, const UObject* AssetToSearch, const UPoseSearchDatabase* Database, bool bContainsIsMandatory)
	{
		TAssetsToSearch* AssetsToSearch = AssetsToSearchPerDatabaseMap.Find(Database);

#if WITH_EDITOR
		// no need to check if Database is indexing if found into AssetsToSearchPerDatabaseMap, since it already passed RequestAsyncBuildIndex successfully in a previous AddToSearchForDatabase call
		if (!AssetsToSearch && (EAsyncBuildIndexResult::Success != FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(Database, ERequestAsyncBuildFlag::ContinueRequest)))
		{
			// database is still indexing.. moving on
			return true;
		}
#endif // WITH_EDITOR

		if (!Database->Contains(AssetToSearch))
		{
			if (bContainsIsMandatory)
			{
				UE_LOG(LogPoseSearch, Error, TEXT("improperly setup UAnimSequenceBase. Database %s doesn't contain UAnimSequenceBase %s"), *Database->GetName(), *AssetToSearch->GetName());
			}
			return false;
		}

		// making sure AssetToSearch is not a databases! later on we could add support for nested databases, but currently we don't support that
		check(Cast<const UPoseSearchDatabase>(AssetToSearch) == nullptr);

		if (AssetsToSearch)
		{
			// an empty TAssetsToSearch associated to Database means we need to search ALL the assets, so we don't need to add this AssetToSearch
			if (!AssetsToSearch->IsEmpty())
			{
				AssetsToSearch->AddUnique(AssetToSearch);
			}
		}
		else
		{
			// no need to AddUnique since it's the first one
			AssetsToSearchPerDatabaseMap.Add(Database).Add(AssetToSearch);
		}

		return false;
	}

	// this function is looking for UPoseSearchDatabase(s) to search for the input AssetToSearch:
	// if AssetToSearch is a database search it ALL,
	// if it's a sequence containing UAnimNotifyState_PoseSearchBranchIn, we add to the search of the dabase UAnimNotifyState_PoseSearchBranchIn::Database the asset AssetToSearch
	// returns bAsyncBuildIndexInProgress
	static bool AddToSearch(TAssetsToSearchPerDatabaseMap& AssetsToSearchPerDatabaseMap, const UObject* AssetToSearch)
	{
		bool bAsyncBuildIndexInProgress = false;
		if (const UAnimSequenceBase* SequenceBase = Cast<const UAnimSequenceBase>(AssetToSearch))
		{
			for (const FAnimNotifyEvent& NotifyEvent : SequenceBase->Notifies)
			{
				if (const UAnimNotifyState_PoseSearchBranchIn* PoseSearchBranchIn = Cast<UAnimNotifyState_PoseSearchBranchIn>(NotifyEvent.NotifyStateClass))
				{
					if (!PoseSearchBranchIn->Database)
					{
						UE_LOG(LogPoseSearch, Error, TEXT("improperly setup UAnimNotifyState_PoseSearchBranchIn with null Database in %s"), *SequenceBase->GetName());
						continue;
					}
					
					// we just skip indexing databases to keep the experience as smooth as possible
					if (AddToSearchForDatabase(AssetsToSearchPerDatabaseMap, SequenceBase, PoseSearchBranchIn->Database, true))
					{
						bAsyncBuildIndexInProgress = true;
					}
				}
			}
		}
		else if (const UPoseSearchDatabase* Database = Cast<UPoseSearchDatabase>(AssetToSearch))
		{
			// we already added Database to AssetsToSearchPerDatabaseMap, so it already successfully passed RequestAsyncBuildIndex
			if (TAssetsToSearch* AssetsToSearch = AssetsToSearchPerDatabaseMap.Find(Database))
			{
				// an empty TAssetsToSearch associated to Database means we need to search ALL the assets
				AssetsToSearch->Reset();
			}
			else
#if WITH_EDITOR
			if (EAsyncBuildIndexResult::Success != FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(Database, ERequestAsyncBuildFlag::ContinueRequest))
			{
				bAsyncBuildIndexInProgress = true;
			}
			else
#endif // WITH_EDITOR
			{
				// an empty TAssetsToSearch associated to Database means we need to search ALL the assets
				AssetsToSearchPerDatabaseMap.Add(Database);
			}
		}

		return bAsyncBuildIndexInProgress;
	}

	static bool IsForceInterrupt(EPoseSearchInterruptMode InterruptMode, const UPoseSearchDatabase* CurrentResultDatabase, const TArray<TObjectPtr<const UPoseSearchDatabase>>& Databases)
	{
		switch (InterruptMode)
		{
		case EPoseSearchInterruptMode::DoNotInterrupt:
			return false;

		case EPoseSearchInterruptMode::InterruptOnDatabaseChange:	// Fall through
		case EPoseSearchInterruptMode::InterruptOnDatabaseChangeAndInvalidateContinuingPose:
			return !Databases.Contains(CurrentResultDatabase);

		case EPoseSearchInterruptMode::ForceInterrupt:				// Fall through
		case EPoseSearchInterruptMode::ForceInterruptAndInvalidateContinuingPose:
			return true;

		default:
			checkNoEntry();
			return false;
		}
	}

	static bool IsInvalidatingContinuingPose(EPoseSearchInterruptMode InterruptMode, const UPoseSearchDatabase* CurrentResultDatabase, const TArray<TObjectPtr<const UPoseSearchDatabase>>& Databases)
	{
		switch (InterruptMode)
		{
		case EPoseSearchInterruptMode::DoNotInterrupt:				// Fall through
		case EPoseSearchInterruptMode::InterruptOnDatabaseChange:	// Fall through
		case EPoseSearchInterruptMode::ForceInterrupt:	
			return false;

		case EPoseSearchInterruptMode::InterruptOnDatabaseChangeAndInvalidateContinuingPose:
			return !Databases.Contains(CurrentResultDatabase);

		case EPoseSearchInterruptMode::ForceInterruptAndInvalidateContinuingPose:
			return true;

		default:
			checkNoEntry();
			return false;
		}
	}

	static bool ShouldUseCachedChannelData(const UPoseSearchDatabase* CurrentResultDatabase, const TArray<TObjectPtr<const UPoseSearchDatabase>>& Databases)
	{
		const UPoseSearchSchema* OneOfTheSchemas = nullptr;
		if (CurrentResultDatabase)
		{
			OneOfTheSchemas = CurrentResultDatabase->Schema;
		}

		for (const TObjectPtr<const UPoseSearchDatabase>& Database : Databases)
		{
			if (ensure(Database))
			{
				if (OneOfTheSchemas != Database->Schema)
				{
					if (OneOfTheSchemas == nullptr)
					{
						OneOfTheSchemas = Database->Schema;
					}
					else
					{
						// we found we need to search multiple schemas
						return true;
					}
				}
			}
		}

		return false;
	}

#if ENABLE_DRAW_DEBUG && ENABLE_ANIM_DEBUG
	class UAnimInstanceProxyProvider : public UAnimInstance
	{
	public:
		static FAnimInstanceProxy* GetAnimInstanceProxy(UAnimInstance* AnimInstance)
		{
			if (AnimInstance)
			{
				return &static_cast<UAnimInstanceProxyProvider*>(AnimInstance)->GetProxyOnAnyThread<FAnimInstanceProxy>();
			}
			return nullptr;
		}
	};
#endif //ENABLE_DRAW_DEBUG && ENABLE_ANIM_DEBUG
}

//////////////////////////////////////////////////////////////////////////
// FMotionMatchingState

void FMotionMatchingState::Reset(const FTransform& ComponentTransform)
{
	CurrentSearchResult.Reset();
	// Set the elapsed time to INFINITY to trigger a search right away
	ElapsedPoseSearchTime = std::numeric_limits<float>::infinity();
	WantedPlayRate = 1.f;
	bJumpedToPose = false;
	
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	ComponentDeltaYaw = 0.f;
	ComponentWorldYaw = FRotator(ComponentTransform.GetRotation()).Yaw;
	AnimationDeltaYaw = 0.f;
PRAGMA_ENABLE_DEPRECATION_WARNINGS

	PoseIndicesHistory.Reset();
}

void FMotionMatchingState::AdjustAssetTime(float AssetTime)
{
	CurrentSearchResult.Update(AssetTime);
}

void FMotionMatchingState::JumpToPose(const FAnimationUpdateContext& Context, const UE::PoseSearch::FSearchResult& Result, int32 MaxActiveBlends, float BlendTime)
{
	// Remember which pose and sequence we're playing from the database
	CurrentSearchResult = Result;

	bJumpedToPose = true;
}

FVector FMotionMatchingState::GetEstimatedFutureRootMotionVelocity() const
{
	using namespace UE::PoseSearch;
	if (CurrentSearchResult.IsValid())
	{
		if (const UPoseSearchFeatureChannel_Trajectory* TrajectoryChannel = CurrentSearchResult.Database->Schema->FindFirstChannelOfType<UPoseSearchFeatureChannel_Trajectory>())
		{
			const FSearchIndex& SearchIndex = CurrentSearchResult.Database->GetSearchIndex();
			if (!SearchIndex.IsValuesEmpty())
			{
				TConstArrayView<float> ResultData = SearchIndex.GetPoseValues(CurrentSearchResult.PoseIdx);
				return TrajectoryChannel->GetEstimatedFutureRootMotionVelocity(ResultData);
			}
		}
	}

	return FVector::ZeroVector;
}

void FMotionMatchingState::UpdateWantedPlayRate(const UE::PoseSearch::FSearchContext& SearchContext, const FFloatInterval& PlayRate, float TrajectorySpeedMultiplier)
{
	if (CurrentSearchResult.IsValid())
	{
		if (!ensure(PlayRate.Min <= PlayRate.Max && PlayRate.Min > UE_KINDA_SMALL_NUMBER))
		{
			UE_LOG(LogPoseSearch, Error, TEXT("Couldn't update the WantedPlayRate in FMotionMatchingState::UpdateWantedPlayRate, because of invalid PlayRate interval (%f, %f)"), PlayRate.Min, PlayRate.Max);
			WantedPlayRate = 1.f;
		}
		else if (!FMath::IsNearlyEqual(PlayRate.Min, PlayRate.Max, UE_KINDA_SMALL_NUMBER))
		{
			TConstArrayView<float> QueryData = SearchContext.GetCachedQuery(CurrentSearchResult.Database->Schema);
			if (!QueryData.IsEmpty())
			{
				if (const UPoseSearchFeatureChannel_Trajectory* TrajectoryChannel = CurrentSearchResult.Database->Schema->FindFirstChannelOfType<UPoseSearchFeatureChannel_Trajectory>())
				{
					TConstArrayView<float> ResultData = CurrentSearchResult.Database->GetSearchIndex().GetPoseValues(CurrentSearchResult.PoseIdx);
					const float EstimatedSpeedRatio = TrajectoryChannel->GetEstimatedSpeedRatio(QueryData, ResultData);

					WantedPlayRate = FMath::Clamp(EstimatedSpeedRatio, PlayRate.Min, PlayRate.Max);
				}
				else
				{
					UE_LOG(LogPoseSearch, Warning,
						TEXT("Couldn't update the WantedPlayRate in FMotionMatchingState::UpdateWantedPlayRate, because Schema '%s' couldn't find a UPoseSearchFeatureChannel_Trajectory channel"),
						*GetNameSafe(CurrentSearchResult.Database->Schema));
				}
			}
		}
		else if (!FMath::IsNearlyZero(TrajectorySpeedMultiplier))
		{
			WantedPlayRate = PlayRate.Min / TrajectorySpeedMultiplier;
		}
		else
		{
			WantedPlayRate = PlayRate.Min;
		}
	}
}

#if UE_POSE_SEARCH_TRACE_ENABLED
void UPoseSearchLibrary::TraceMotionMatching(UE::PoseSearch::FSearchContext& SearchContext, FMotionMatchingState& CurrentState, float DeltaTime, bool bSearch, float RecordingTime)
{
	using namespace UE::PoseSearch;
	
	uint32 SearchId = 787;

	FTraceMotionMatchingStateMessage TraceState;
	FSearchResult& CurrentResult = CurrentState.CurrentSearchResult;
	const float ElapsedPoseSearchTime = CurrentState.ElapsedPoseSearchTime;

	const int32 AnimInstancesNum = SearchContext.GetAnimInstances().Num();
	TraceState.SkeletalMeshComponentIds.SetNum(AnimInstancesNum);

	for (int32 AnimInstanceIndex = 0; AnimInstanceIndex < AnimInstancesNum; ++AnimInstanceIndex)
	{
		if (const UAnimInstance* AnimInstance = SearchContext.GetAnimInstances()[AnimInstanceIndex])
		{
			const UObject* SkeletalMeshComponent = AnimInstance->GetOuter();

			TRACE_OBJECT(AnimInstance);

			TraceState.SkeletalMeshComponentIds[AnimInstanceIndex] = FObjectTrace::GetObjectId(SkeletalMeshComponent);

			SearchId = HashCombineFast(SearchId, GetTypeHash(FObjectTrace::GetObjectId(AnimInstance)));
		}
	}

	TraceState.Roles.SetNum(AnimInstancesNum);
	for (const FRoleToIndexPair& RoleToIndexPair : SearchContext.GetRoleToIndex())
	{
		TraceState.Roles[RoleToIndexPair.Value] = RoleToIndexPair.Key;
	}

	SearchId = HashCombineFast(SearchId, GetTypeHash(TraceState.Roles));

	// @todo: do we need to hash pose history names in SearchId as well?
	TraceState.PoseHistories.SetNum(AnimInstancesNum);
	for (int32 AnimInstanceIndex = 0; AnimInstanceIndex < AnimInstancesNum; ++AnimInstanceIndex)
	{
		TraceState.PoseHistories[AnimInstanceIndex].InitFrom(SearchContext.GetPoseHistories()[AnimInstanceIndex]);
	}

	TArray<uint64, TInlineAllocator<64>> DatabaseIds;
	int32 DbEntryIdx = 0;
	const int32 CurrentPoseIdx = bSearch && CurrentResult.PoseCost.IsValid() ? CurrentResult.PoseIdx : INDEX_NONE;
	TraceState.DatabaseEntries.SetNum(SearchContext.GetBestPoseCandidatesMap().Num());
	for (TPair<const UPoseSearchDatabase*, FSearchContext::FBestPoseCandidates> DatabaseBestPoseCandidates : SearchContext.GetBestPoseCandidatesMap())
	{
		const UPoseSearchDatabase* Database = DatabaseBestPoseCandidates.Key;
		check(Database);

		FTraceMotionMatchingStateDatabaseEntry& DbEntry = TraceState.DatabaseEntries[DbEntryIdx];

		// if throttling is on, the continuing pose can be valid, but no actual search occurred, so the query will not be cached, and we need to build it
		DbEntry.QueryVector = SearchContext.GetOrBuildQuery(Database->Schema);
		DbEntry.DatabaseId = FTraceMotionMatchingStateMessage::GetIdFromObject(Database);
		DatabaseIds.Add(DbEntry.DatabaseId);

		for (int32 CandidateIdx = 0; CandidateIdx < DatabaseBestPoseCandidates.Value.Num(); ++CandidateIdx)
		{
			const FSearchContext::FPoseCandidate PoseCandidate = DatabaseBestPoseCandidates.Value.GetUnsortedCandidate(CandidateIdx);

			FTraceMotionMatchingStatePoseEntry PoseEntry;
			PoseEntry.DbPoseIdx = PoseCandidate.PoseIdx;
			PoseEntry.Cost = PoseCandidate.Cost;
			PoseEntry.PoseCandidateFlags = PoseCandidate.PoseCandidateFlags;
			if (CurrentPoseIdx == PoseCandidate.PoseIdx && CurrentResult.Database.Get() == Database)
			{
				check(EnumHasAnyFlags(PoseEntry.PoseCandidateFlags, EPoseCandidateFlags::Valid_Pose | EPoseCandidateFlags::Valid_ContinuingPose));

				EnumAddFlags(PoseEntry.PoseCandidateFlags, EPoseCandidateFlags::Valid_CurrentPose);

				TraceState.CurrentDbEntryIdx = DbEntryIdx;
				TraceState.CurrentPoseEntryIdx = DbEntry.PoseEntries.Add(PoseEntry);
			}
			else
			{
				DbEntry.PoseEntries.Add(PoseEntry);
			}
		}

		++DbEntryIdx;
	}

	DatabaseIds.Sort();
	SearchId = HashCombineFast(SearchId, GetTypeHash(DatabaseIds));

	if (DeltaTime > SMALL_NUMBER)
	{
		// simulation
		if (SearchContext.AnyCachedQuery())
		{
			TraceState.SimLinearVelocity = 0.f;
			TraceState.SimAngularVelocity = 0.f;

			const int32 NumRoles = SearchContext.GetRoleToIndex().Num();
			for (const FRoleToIndexPair& RoleToIndexPair : SearchContext.GetRoleToIndex())
			{
				const FRole& Role = RoleToIndexPair.Key;

				const FTransform PrevRoot = SearchContext.GetWorldBoneTransformAtTime(-DeltaTime, Role, RootSchemaBoneIdx);
				const FTransform CurrRoot = SearchContext.GetWorldBoneTransformAtTime(0.f, Role, RootSchemaBoneIdx);
				
				const FTransform SimDelta = CurrRoot.GetRelativeTransform(PrevRoot);
				TraceState.SimLinearVelocity += SimDelta.GetTranslation().Size() / (DeltaTime * NumRoles);
				TraceState.SimAngularVelocity += FMath::RadiansToDegrees(SimDelta.GetRotation().GetAngle()) / (DeltaTime * NumRoles);
			}
		}

		const FSearchIndexAsset* SearchIndexAsset = CurrentResult.GetSearchIndexAsset();
		const UPoseSearchDatabase* CurrentResultDatabase = CurrentResult.Database.Get();
		if (SearchIndexAsset && CurrentResultDatabase)
		{
			const FPoseSearchDatabaseAnimationAssetBase* DatabaseAsset = CurrentResultDatabase->GetDatabaseAnimationAsset<FPoseSearchDatabaseAnimationAssetBase>(*SearchIndexAsset);
			check(DatabaseAsset);
			if (UAnimationAsset* AnimationAsset = Cast<UAnimationAsset>(DatabaseAsset->GetAnimationAsset()))
			{
				// Simulate the time step to get accurate root motion prediction for this frame.
				FAnimationAssetSampler Sampler(AnimationAsset);

				const float TimeStep = DeltaTime * CurrentState.WantedPlayRate;
				const FTransform PrevRoot = Sampler.ExtractRootTransform(CurrentResult.AssetTime);
				const FTransform CurrRoot = Sampler.ExtractRootTransform(CurrentResult.AssetTime + TimeStep);
				const FTransform RootMotionTransformDelta = PrevRoot.GetRelativeTransform(CurrRoot);
				TraceState.AnimLinearVelocity = RootMotionTransformDelta.GetTranslation().Size() / DeltaTime;
				TraceState.AnimAngularVelocity = FMath::RadiansToDegrees(RootMotionTransformDelta.GetRotation().GetAngle()) / DeltaTime;

				// Need another root motion extraction for non-playrate version in case acceleration isn't the same.
				const FTransform CurrRootNoTimescale = Sampler.ExtractRootTransform(CurrentResult.AssetTime + DeltaTime);
				const FTransform RootMotionTransformDeltaNoTimescale = PrevRoot.GetRelativeTransform(CurrRootNoTimescale);
				TraceState.AnimLinearVelocityNoTimescale = RootMotionTransformDeltaNoTimescale.GetTranslation().Size() / DeltaTime;
				TraceState.AnimAngularVelocityNoTimescale = FMath::RadiansToDegrees(RootMotionTransformDeltaNoTimescale.GetRotation().GetAngle()) / DeltaTime;
			}
		}
		TraceState.Playrate = CurrentState.WantedPlayRate;
	}

	TraceState.ElapsedPoseSearchTime = ElapsedPoseSearchTime;
	TraceState.AssetPlayerTime = CurrentResult.AssetTime;
	TraceState.DeltaTime = DeltaTime;

	TraceState.RecordingTime = RecordingTime;
	TraceState.SearchBestCost = CurrentResult.PoseCost.GetTotalCost();
#if WITH_EDITOR && ENABLE_ANIM_DEBUG
	TraceState.SearchBruteForceCost = CurrentResult.BruteForcePoseCost.GetTotalCost();
	TraceState.SearchBestPosePos = CurrentResult.BestPosePos;
#else // WITH_EDITOR && ENABLE_ANIM_DEBUG
	TraceState.SearchBruteForceCost = 0.f;
	TraceState.SearchBestPosePos = 0;
#endif // WITH_EDITOR && ENABLE_ANIM_DEBUG

	TraceState.Cycle = FPlatformTime::Cycles64();

	// @todo: avoid publishing duplicated TraceState in ALL the AnimInstances! -currently necessary for multi character-
	for (const UAnimInstance* AnimInstance : SearchContext.GetAnimInstances())
	{
		TraceState.AnimInstanceId = FObjectTrace::GetObjectId(AnimInstance);
		TraceState.NodeId = SearchId;
		TraceState.Output();
	}
}
#endif // UE_POSE_SEARCH_TRACE_ENABLED

void UPoseSearchLibrary::UpdateMotionMatchingState(
	const FAnimationUpdateContext& Context,
	const TArray<TObjectPtr<const UPoseSearchDatabase>>& Databases,
	float BlendTime,
	int32 MaxActiveBlends,
	const FFloatInterval& PoseJumpThresholdTime,
	float PoseReselectHistory,
	float SearchThrottleTime,
	const FFloatInterval& PlayRate,
	FMotionMatchingState& InOutMotionMatchingState,
	EPoseSearchInterruptMode InterruptMode,
	bool bShouldSearch,
	bool bShouldUseCachedChannelData,
	bool bDebugDrawQuery,
	bool bDebugDrawCurResult)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_PoseSearch_Update);

	using namespace UE::PoseSearch;

	check(Context.AnimInstanceProxy);

	if (Databases.IsEmpty())
	{
		Context.LogMessage(
			EMessageSeverity::Error,
			LOCTEXT("NoDatabases", "No database assets provided for motion matching."));
		return;
	}

	const float DeltaTime = Context.GetDeltaTime();

	InOutMotionMatchingState.bJumpedToPose = false;

	const IPoseHistory* PoseHistory = nullptr;
	if (FPoseHistoryProvider* PoseHistoryProvider = Context.GetMessage<FPoseHistoryProvider>())
	{
		PoseHistory = &PoseHistoryProvider->GetPoseHistory();
	}

	FMemMark Mark(FMemStack::Get());
	const UAnimInstance* AnimInstance = Cast<const UAnimInstance>(Context.AnimInstanceProxy->GetAnimInstanceObject());
	check(AnimInstance);

	const UPoseSearchDatabase* CurrentResultDatabase = InOutMotionMatchingState.CurrentSearchResult.Database.Get();
	if (IsInvalidatingContinuingPose(InterruptMode, CurrentResultDatabase, Databases))
	{
		InOutMotionMatchingState.CurrentSearchResult.Reset();
	}

	FSearchContext SearchContext(0.f, &InOutMotionMatchingState.PoseIndicesHistory, InOutMotionMatchingState.CurrentSearchResult, PoseJumpThresholdTime);
	SearchContext.AddRole(DefaultRole, AnimInstance, PoseHistory);

	const bool bCanAdvance = InOutMotionMatchingState.CurrentSearchResult.CanAdvance(DeltaTime);

	// If we can't advance or enough time has elapsed since the last pose jump then search
	const bool bSearch = !bCanAdvance || (bShouldSearch && (InOutMotionMatchingState.ElapsedPoseSearchTime >= SearchThrottleTime));
	if (bSearch)
	{
		InOutMotionMatchingState.ElapsedPoseSearchTime = 0.f;
		const bool bForceInterrupt = IsForceInterrupt(InterruptMode, CurrentResultDatabase, Databases);
		const bool bSearchContinuingPose = !bForceInterrupt && bCanAdvance;

		// calculating if it's worth bUseCachedChannelData (if we potentially have to build query with multiple schemas)
		SearchContext.SetUseCachedChannelData(bShouldUseCachedChannelData && ShouldUseCachedChannelData(bSearchContinuingPose ? CurrentResultDatabase : nullptr, Databases));

		FSearchResult SearchResult;
		// Evaluate continuing pose
		if (bSearchContinuingPose)
		{
			SearchResult = CurrentResultDatabase->SearchContinuingPose(SearchContext);
			SearchContext.UpdateCurrentBestCost(SearchResult.PoseCost);
		}

		bool bJumpToPose = false;
		for (const TObjectPtr<const UPoseSearchDatabase>& Database : Databases)
		{
			if (ensure(Database))
			{
				const FSearchResult NewSearchResult = Database->Search(SearchContext);

#if WITH_EDITOR && ENABLE_ANIM_DEBUG && UE_POSE_SEARCH_TRACE_ENABLED
				const FPoseSearchCost BestBruteForcePoseCost = NewSearchResult.BruteForcePoseCost < SearchResult.BruteForcePoseCost ? NewSearchResult.BruteForcePoseCost : SearchResult.BruteForcePoseCost;
#endif // WITH_EDITOR && ENABLE_ANIM_DEBUG && UE_POSE_SEARCH_TRACE_ENABLED

				if (NewSearchResult.PoseCost < SearchResult.PoseCost)
				{
					bJumpToPose = true;
					SearchResult = NewSearchResult;
					SearchContext.UpdateCurrentBestCost(SearchResult.PoseCost);
				}

#if WITH_EDITOR && ENABLE_ANIM_DEBUG && UE_POSE_SEARCH_TRACE_ENABLED
				SearchResult.BruteForcePoseCost = BestBruteForcePoseCost;
#endif // WITH_EDITOR && ENABLE_ANIM_DEBUG && UE_POSE_SEARCH_TRACE_ENABLED
			}
		}

#if WITH_EDITOR
		// resetting CurrentSearchResult if any DDC indexing on the requested databases is still in progress
		if (SearchContext.IsAsyncBuildIndexInProgress())
		{
			InOutMotionMatchingState.CurrentSearchResult.Reset();
		}
#endif // WITH_EDITOR

#if !NO_LOGGING
		if (!SearchResult.IsValid())
		{
			TStringBuilder<1024> StringBuilder;
			StringBuilder << "UPoseSearchLibrary::UpdateMotionMatchingState invalid search result : ForceInterrupt [";
			StringBuilder << bForceInterrupt;
			StringBuilder << "], CanAdvance [";
			StringBuilder << bCanAdvance;
			StringBuilder << "], Indexing [";

			bool bIsIndexing = false;
#if WITH_EDITOR
			bIsIndexing = SearchContext.IsAsyncBuildIndexInProgress();
#endif // WITH_EDITOR
			StringBuilder << bIsIndexing;

			StringBuilder << "], Databases [";

			for (int32 DatabaseIndex = 0; DatabaseIndex < Databases.Num(); ++DatabaseIndex)
			{
				StringBuilder << GetNameSafe(Databases[DatabaseIndex]);
				if (DatabaseIndex != Databases.Num() - 1)
				{
					StringBuilder << ", ";
				}
			}
			
			StringBuilder << "] ";

			FString String = StringBuilder.ToString();

			if (bIsIndexing)
			{
				UE_LOG(LogPoseSearch, Log, TEXT("%s"), *String);
			}
			else
			{
				UE_LOG(LogPoseSearch, Warning, TEXT("%s"), *String);
			}
		}
#endif // !NO_LOGGING

		if (bJumpToPose)
		{
			InOutMotionMatchingState.JumpToPose(Context, SearchResult, MaxActiveBlends, BlendTime);
		}
		else
		{
			// copying few properties of SearchResult into CurrentSearchResult to facilitate debug drawing
#if WITH_EDITOR && ENABLE_ANIM_DEBUG && UE_POSE_SEARCH_TRACE_ENABLED
			InOutMotionMatchingState.CurrentSearchResult.BruteForcePoseCost = SearchResult.BruteForcePoseCost;
#endif // WITH_EDITOR && ENABLE_ANIM_DEBUG && UE_POSE_SEARCH_TRACE_ENABLED
			InOutMotionMatchingState.CurrentSearchResult.PoseCost = SearchResult.PoseCost;
		}
	}
	else
	{
		InOutMotionMatchingState.ElapsedPoseSearchTime += DeltaTime;
	}

	// @todo: consider moving this into if (bSearch) to avoid calling SearchContext.GetCachedQuery if no search is required
	InOutMotionMatchingState.UpdateWantedPlayRate(SearchContext, PlayRate, PoseHistory ? PoseHistory->GetTrajectorySpeedMultiplier() : 1.f);

	InOutMotionMatchingState.PoseIndicesHistory.Update(InOutMotionMatchingState.CurrentSearchResult, DeltaTime, PoseReselectHistory);

#if UE_POSE_SEARCH_TRACE_ENABLED
	// Record debugger details
	if (IsTracing(Context))
	{
		TraceMotionMatching(SearchContext, InOutMotionMatchingState, DeltaTime, bSearch,
			AnimInstance ? FObjectTrace::GetWorldElapsedTime(AnimInstance->GetWorld()) : 0.f);
	}
#endif // UE_POSE_SEARCH_TRACE_ENABLED

#if WITH_EDITORONLY_DATA && ENABLE_ANIM_DEBUG
	const FSearchResult& CurResult = InOutMotionMatchingState.CurrentSearchResult;
	if (bDebugDrawQuery || bDebugDrawCurResult)
	{
		const UPoseSearchDatabase* CurResultDatabase = CurResult.Database.Get();

#if WITH_EDITOR
		if (EAsyncBuildIndexResult::Success == FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(CurResultDatabase, ERequestAsyncBuildFlag::ContinueRequest))
#endif // WITH_EDITOR
		{
			FAnimInstanceProxy* AnimInstanceProxy = Context.AnimInstanceProxy;
			const TArrayView<FAnimInstanceProxy*> AnimInstanceProxies = MakeArrayView(&AnimInstanceProxy, 1);
			
			if (bDebugDrawCurResult)
			{
				FDebugDrawParams DrawParams(AnimInstanceProxies, SearchContext.GetPoseHistories(), SearchContext.GetRoleToIndex(), CurResultDatabase);
				DrawParams.DrawFeatureVector(CurResult.PoseIdx);
			}

			if (bDebugDrawQuery)
			{
				FDebugDrawParams DrawParams(AnimInstanceProxies, SearchContext.GetPoseHistories(), SearchContext.GetRoleToIndex(), CurResultDatabase, EDebugDrawFlags::DrawQuery);
				DrawParams.DrawFeatureVector(SearchContext.GetOrBuildQuery(CurResultDatabase->Schema));
			}
		}
	}
#endif
}

void UPoseSearchLibrary::IsAnimationAssetLooping(const UObject* Asset, bool& bIsAssetLooping)
{
	if (const UAnimSequenceBase* SequenceBase = Cast<const UAnimSequenceBase>(Asset))
	{
		bIsAssetLooping = SequenceBase->bLoop;
	}
	else if (const UBlendSpace* BlendSpace = Cast<const UBlendSpace>(Asset))
	{
		bIsAssetLooping = BlendSpace->bLoop;
	}
	else if (const UMultiAnimAsset* MultiAnimAsset = Cast<const UMultiAnimAsset>(Asset))
	{
		bIsAssetLooping = MultiAnimAsset->IsLooping();
	}
	else
	{
		bIsAssetLooping = false;
	}
}

void UPoseSearchLibrary::GetDatabaseTags(const UPoseSearchDatabase* Database, TArray<FName>& Tags)
{
	if (Database)
	{
		Tags = Database->Tags;
	}
	else
	{
		Tags.Reset();
	}
}

void UPoseSearchLibrary::MotionMatch(
	UAnimInstance* AnimInstance,
	TArray<UObject*> AssetsToSearch,
	const FName PoseHistoryName,
	const FPoseSearchContinuingProperties ContinuingProperties,
	const FPoseSearchFutureProperties Future,
	FPoseSearchBlueprintResult& Result)
{
	using namespace UE::PoseSearch;

	FMemMark Mark(FMemStack::Get());

	TArray<UAnimInstance*, TInlineAllocator<PreallocatedRolesNum, TMemStackAllocator<>>> AnimInstances;
	AnimInstances.Add(AnimInstance);

	TArray<FName, TInlineAllocator<PreallocatedRolesNum, TMemStackAllocator<>>> Roles;
	Roles.Add(UE::PoseSearch::DefaultRole);

	TArray<const UObject*>& AssetsToSearchConst = reinterpret_cast<TArray<const UObject*>&>(AssetsToSearch);
	MotionMatch(AnimInstances, Roles, AssetsToSearchConst, PoseHistoryName, ContinuingProperties, Future, Result);
}

void UPoseSearchLibrary::MotionMatch(
	const TArrayView<UAnimInstance*> AnimInstances,
	const TArrayView<const UE::PoseSearch::FRole> Roles,
	const TArrayView<const UObject*> AssetsToSearch,
	const FName PoseHistoryName,
	const FPoseSearchContinuingProperties& ContinuingProperties,
	const FPoseSearchFutureProperties& Future,
	FPoseSearchBlueprintResult& Result)
{
	using namespace UE::Anim;
	using namespace UE::PoseSearch;

	Result.SelectedAnimation = nullptr;
	Result.SelectedTime = 0.f;
	Result.bIsContinuingPoseSearch = false;
	Result.bLoop = false;
	Result.bIsMirrored = false;
	Result.BlendParameters = FVector::ZeroVector;
	Result.SelectedDatabase = nullptr;
	Result.SearchCost = MAX_flt;

	if (AnimInstances.IsEmpty() || AnimInstances.Num() != Roles.Num())
	{
		UE_LOG(LogPoseSearch, Error, TEXT("UPoseSearchLibrary::MotionMatch - invalid input AnimInstances or Roles"));
		return;
	}
	
	for (UAnimInstance* AnimInstance : AnimInstances)
	{
		if (!AnimInstance)
		{
			UE_LOG(LogPoseSearch, Error, TEXT("UPoseSearchLibrary::MotionMatch - null AnimInstances"));
			return;
		}

		if (!AnimInstance->CurrentSkeleton)
		{
			UE_LOG(LogPoseSearch, Error, TEXT("UPoseSearchLibrary::MotionMatch - null AnimInstances->CurrentSkeleton"));
			return;
		}
	}

	FMemMark Mark(FMemStack::Get());

	TArray<const UE::PoseSearch::IPoseHistory*, TInlineAllocator<PreallocatedRolesNum, TMemStackAllocator<>>> PoseHistories;
	for (UAnimInstance* AnimInstance : AnimInstances)
	{
		if (const FAnimNode_PoseSearchHistoryCollector_Base* PoseHistoryNode = FindPoseHistoryNode(PoseHistoryName, AnimInstance))
		{
			PoseHistories.Add(&PoseHistoryNode->GetPoseHistory());
		}
	}

	if (PoseHistories.Num() != AnimInstances.Num())
	{
		UE_LOG(LogPoseSearch, Error, TEXT("UPoseSearchLibrary::MotionMatch - Couldn't find pose history with name '%s'"), *PoseHistoryName.ToString());
		return;
	}

	const FSearchResult SearchResult = MotionMatch(AnimInstances, Roles, PoseHistories, AssetsToSearch, ContinuingProperties, Future);
	if (SearchResult.IsValid())
	{
		const UPoseSearchDatabase* Database = SearchResult.Database.Get();
		const FSearchIndexAsset* SearchIndexAsset = SearchResult.GetSearchIndexAsset();
		if (const FPoseSearchDatabaseAnimationAssetBase* DatabaseAsset = Database->GetDatabaseAnimationAsset<FPoseSearchDatabaseAnimationAssetBase>(*SearchIndexAsset))
		{
			Result.SelectedAnimation = DatabaseAsset->GetAnimationAsset();
			Result.SelectedTime = SearchResult.AssetTime;
			Result.bIsContinuingPoseSearch = SearchResult.bIsContinuingPoseSearch;
			Result.bLoop = SearchIndexAsset->IsLooping();
			Result.bIsMirrored = SearchIndexAsset->IsMirrored();
			Result.BlendParameters = SearchIndexAsset->GetBlendParameters();
			Result.SelectedDatabase = Database;
			Result.SearchCost = SearchResult.PoseCost.GetTotalCost();
			
			// figuring out the WantedPlayRate
			Result.WantedPlayRate = 1.f;
			if (Future.Animation && Future.IntervalTime > 0.f)
			{
				if (const UPoseSearchFeatureChannel_PermutationTime* PermutationTimeChannel = Database->Schema->FindFirstChannelOfType<UPoseSearchFeatureChannel_PermutationTime>())
				{
					const FSearchIndex& SearchIndex = Database->GetSearchIndex();
					if (!SearchIndex.IsValuesEmpty())
					{
						TConstArrayView<float> ResultData = Database->GetSearchIndex().GetPoseValues(SearchResult.PoseIdx);
						const float ActualIntervalTime = PermutationTimeChannel->GetPermutationTime(ResultData);
						Result.WantedPlayRate = ActualIntervalTime / Future.IntervalTime;
					}
				}
			}
		}
	}
}

UE::PoseSearch::FSearchResult UPoseSearchLibrary::MotionMatch(
	const TArrayView<UAnimInstance*> AnimInstances,
	const TArrayView<const UE::PoseSearch::FRole> Roles,
	const TArrayView<const UE::PoseSearch::IPoseHistory*> PoseHistories, 
	const TArrayView<const UObject*> AssetsToSearch,
	const FPoseSearchContinuingProperties& ContinuingProperties,
	const FPoseSearchFutureProperties& Future)
{
	check(!AnimInstances.IsEmpty() && AnimInstances.Num() == Roles.Num() && AnimInstances.Num() == PoseHistories.Num());

	using namespace UE::PoseSearch;

	FSearchResult SearchResult;

	FMemMark Mark(FMemStack::Get());

	TArray<const UE::PoseSearch::IPoseHistory*, TInlineAllocator<PreallocatedRolesNum, TMemStackAllocator<>>> InternalPoseHistories;
	InternalPoseHistories = PoseHistories;

	// MemStackPoseHistories will hold future poses to match AssetSamplerBase (at FutureAnimationStartTime) TimeToFutureAnimationStart seconds in the future
	TArray<FMemStackPoseHistory, TInlineAllocator<PreallocatedRolesNum, TMemStackAllocator<>>> MemStackPoseHistories;
	float FutureIntervalTime = Future.IntervalTime;
	if (Future.Animation)
	{
		MemStackPoseHistories.SetNum(InternalPoseHistories.Num());

		float FutureAnimationTime = Future.AnimationTime;
		if (FutureAnimationTime < FiniteDelta)
		{
			UE_LOG(LogPoseSearch, Warning, TEXT("UPoseSearchLibrary::MotionMatch - provided Future.AnimationTime (%f) is too small to be able to calculate velocities. Clamping it to minimum value of %f"), FutureAnimationTime, FiniteDelta);
			FutureAnimationTime = FiniteDelta;
		}

		const float MinFutureIntervalTime = FiniteDelta + UE_KINDA_SMALL_NUMBER;
		if (FutureIntervalTime < MinFutureIntervalTime)
		{
			UE_LOG(LogPoseSearch, Warning, TEXT("UPoseSearchLibrary::MotionMatch - provided TimeToFutureAnimationStart (%f) is too small. Clamping it to minimum value of %f"), FutureIntervalTime, MinFutureIntervalTime);
			FutureIntervalTime = MinFutureIntervalTime;
		}

		for (int32 RoleIndex = 0; RoleIndex < Roles.Num(); ++RoleIndex)
		{
			MemStackPoseHistories[RoleIndex].Init(InternalPoseHistories[RoleIndex]);

			// extracting 2 poses to be able to calculate velocities
			FCSPose<FCompactPose> ComponentSpacePose;
			FCompactPose Pose;
			FBlendedCurve Curves;
			Pose.SetBoneContainer(&AnimInstances[RoleIndex]->GetRequiredBonesOnAnyThread());

			// @todo: add input BlendParameters to support sampling FutureAnimation blendspaces and support for multi character
			const UAnimationAsset* AnimationAsset = Cast<UAnimationAsset>(Future.Animation);
			if (!AnimationAsset)
			{
				if (const UMultiAnimAsset* MultiAnimAsset = Cast<UMultiAnimAsset>(Future.Animation))
				{
					AnimationAsset = MultiAnimAsset->GetAnimationAsset(Roles[RoleIndex]);
				}
				else
				{
					checkNoEntry();
				}
			}

			const FAnimationAssetSampler Sampler(AnimationAsset);
			for (int32 i = 0; i < 2; ++i)
			{
				const float FuturePoseExtractionTime = FutureAnimationTime + (i - 1) * FiniteDelta;
				const float FuturePoseAnimationTime = FutureIntervalTime + (i - 1) * FiniteDelta;

				Sampler.ExtractPose(FuturePoseExtractionTime, Pose, Curves);
				ComponentSpacePose.InitPose(Pose);
				MemStackPoseHistories[RoleIndex].AddFuturePose(FuturePoseAnimationTime, ComponentSpacePose);
			}

#if ENABLE_DRAW_DEBUG && ENABLE_ANIM_DEBUG
			if (FAnimInstanceProxy* AnimInstanceProxy = UAnimInstanceProxyProvider::GetAnimInstanceProxy(AnimInstances[RoleIndex]))
			{
				MemStackPoseHistories[RoleIndex].DebugDraw(*AnimInstanceProxy, FColor::Orange);
			}
#endif // ENABLE_DRAW_DEBUG && ENABLE_ANIM_DEBUG

			InternalPoseHistories[RoleIndex] = MemStackPoseHistories[RoleIndex].GetThisOrPoseHistory();
		}
	}

	FSearchResult ReconstructedPreviousSearchResult;
	FSearchContext SearchContext(FutureIntervalTime, nullptr, ReconstructedPreviousSearchResult);

	// @todo: all assets in AssetsToSearch should have a consistent Roles requirements, or else the search will throw an error!
	for (int32 RoleIndex = 0; RoleIndex < Roles.Num(); ++RoleIndex)
	{
		SearchContext.AddRole(Roles[RoleIndex], AnimInstances[RoleIndex], InternalPoseHistories[RoleIndex]);
	}

	TAssetsToSearchPerDatabaseMap AssetsToSearchPerDatabaseMap;
	
	bool bAsyncBuildIndexInProgress = false;

	// collecting all the possible continuing pose search (it could be multiple searches, but most likely only one)
	const float DeltaSeconds = AnimInstances[0] ? AnimInstances[0]->GetDeltaSeconds() : FiniteDelta;
	if (const UObject* PlayingAnimationAsset = ContinuingProperties.PlayingAsset.Get())
	{
		// checking if PlayingAnimationAsset has an associated database
		if (AddToSearch(AssetsToSearchPerDatabaseMap, PlayingAnimationAsset))
		{
			bAsyncBuildIndexInProgress = true;
		}
		
		// checking if any of the AssetsToSearch (databse) contains PlayingAnimationAsset
		for (const UObject* AssetToSearch : AssetsToSearch)
		{
			if (const UPoseSearchDatabase* Database = Cast<UPoseSearchDatabase>(AssetToSearch))
			{
				// since it cannot be a database we can directly add it to AssetsToSearchPerDatabaseMap
				if (AddToSearchForDatabase(AssetsToSearchPerDatabaseMap, PlayingAnimationAsset, Database, false))
				{
					bAsyncBuildIndexInProgress = true;
				}
			}
		}

		for (const TAssetsToSearchPerDatabasePair& AssetsToSearchPerDatabasePair : AssetsToSearchPerDatabaseMap)
		{
			const UPoseSearchDatabase* Database = AssetsToSearchPerDatabasePair.Key;
			check(Database);

			const FSearchIndex& SearchIndex = Database->GetSearchIndex();
			for (int32 AssetIndex : Database->GetAssetIndexesForSourceAsset(PlayingAnimationAsset))
			{
				const FSearchIndexAsset& SearchIndexAsset = SearchIndex.Assets[AssetIndex];

				const float FirstSampleTime = SearchIndexAsset.GetFirstSampleTime(Database->Schema->SampleRate);
				const float LastSampleTime = SearchIndexAsset.GetLastSampleTime(Database->Schema->SampleRate);

				bool bCanAdvance = true;
				if (SearchIndexAsset.IsLooping())
				{
					const float DeltaSampleTime = LastSampleTime - FirstSampleTime;
					if (DeltaSampleTime < UE_SMALL_NUMBER)
					{
						ReconstructedPreviousSearchResult.AssetTime = FirstSampleTime;
					}
					else if (ContinuingProperties.PlayingAssetAccumulatedTime < FirstSampleTime)
					{
						ReconstructedPreviousSearchResult.AssetTime = FMath::Fmod(ContinuingProperties.PlayingAssetAccumulatedTime - FirstSampleTime, DeltaSampleTime) + DeltaSampleTime + FirstSampleTime;
					}
					else if (ContinuingProperties.PlayingAssetAccumulatedTime > LastSampleTime)
					{
						ReconstructedPreviousSearchResult.AssetTime = FMath::Fmod(ContinuingProperties.PlayingAssetAccumulatedTime - FirstSampleTime, DeltaSampleTime) + FirstSampleTime;
					}
					else
					{
						ReconstructedPreviousSearchResult.AssetTime = ContinuingProperties.PlayingAssetAccumulatedTime;
					}
				}
				else
				{
					const float MaxTimeToBeAbleToContinuingPlayingAnimation = LastSampleTime - DeltaSeconds;
					bCanAdvance = ContinuingProperties.PlayingAssetAccumulatedTime >= FirstSampleTime && ContinuingProperties.PlayingAssetAccumulatedTime < MaxTimeToBeAbleToContinuingPlayingAnimation;
					ReconstructedPreviousSearchResult.AssetTime = ContinuingProperties.PlayingAssetAccumulatedTime;
				}

				if (bCanAdvance)
				{
					ReconstructedPreviousSearchResult.Database = Database;
					ReconstructedPreviousSearchResult.PoseIdx = Database->GetPoseIndexFromTime(ContinuingProperties.PlayingAssetAccumulatedTime, SearchIndexAsset);
					SearchContext.UpdateCurrentResultPoseVector();

					const FSearchResult NewSearchResult = Database->SearchContinuingPose(SearchContext);

#if WITH_EDITOR && ENABLE_ANIM_DEBUG && UE_POSE_SEARCH_TRACE_ENABLED
					const FPoseSearchCost BestBruteForcePoseCost = NewSearchResult.BruteForcePoseCost < SearchResult.BruteForcePoseCost ? NewSearchResult.BruteForcePoseCost : SearchResult.BruteForcePoseCost;
#endif // WITH_EDITOR && ENABLE_ANIM_DEBUG && UE_POSE_SEARCH_TRACE_ENABLED

					if (NewSearchResult.PoseCost < SearchResult.PoseCost)
					{
						SearchResult = NewSearchResult;
						SearchContext.UpdateCurrentBestCost(SearchResult.PoseCost);
					}
								
#if WITH_EDITOR && ENABLE_ANIM_DEBUG && UE_POSE_SEARCH_TRACE_ENABLED
					SearchResult.BruteForcePoseCost = BestBruteForcePoseCost;
#endif // WITH_EDITOR && ENABLE_ANIM_DEBUG && UE_POSE_SEARCH_TRACE_ENABLED
				}
			}
		}

		AssetsToSearchPerDatabaseMap.Reset();
	}

	// collecting all the other databases searches
	if (!AssetsToSearch.IsEmpty())
	{
		for (const UObject* AssetToSearch : AssetsToSearch)
		{
			if (AddToSearch(AssetsToSearchPerDatabaseMap, AssetToSearch))
			{
				bAsyncBuildIndexInProgress = true;
			}
		}

		for (const TAssetsToSearchPerDatabasePair& AssetsToSearchPerDatabasePair : AssetsToSearchPerDatabaseMap)
		{
			const UPoseSearchDatabase* Database = AssetsToSearchPerDatabasePair.Key;
			check(Database);

			SearchContext.SetAssetsToConsider(AssetsToSearchPerDatabasePair.Value);

			const FSearchResult NewSearchResult = Database->Search(SearchContext);

#if WITH_EDITOR && ENABLE_ANIM_DEBUG && UE_POSE_SEARCH_TRACE_ENABLED
			const FPoseSearchCost BestBruteForcePoseCost = NewSearchResult.BruteForcePoseCost < SearchResult.BruteForcePoseCost ? NewSearchResult.BruteForcePoseCost : SearchResult.BruteForcePoseCost;
#endif // WITH_EDITOR && ENABLE_ANIM_DEBUG && UE_POSE_SEARCH_TRACE_ENABLED

			if (NewSearchResult.PoseCost < SearchResult.PoseCost)
			{
				SearchResult = NewSearchResult;
				SearchContext.UpdateCurrentBestCost(SearchResult.PoseCost);
			}

#if WITH_EDITOR && ENABLE_ANIM_DEBUG && UE_POSE_SEARCH_TRACE_ENABLED
			SearchResult.BruteForcePoseCost = BestBruteForcePoseCost;
#endif // WITH_EDITOR && ENABLE_ANIM_DEBUG && UE_POSE_SEARCH_TRACE_ENABLED
		}
	}

#if WITH_EDITOR
	if (bAsyncBuildIndexInProgress)
	{
		SearchContext.SetAsyncBuildIndexInProgress();
	}
#endif // WITH_EDITOR

#if ENABLE_DRAW_DEBUG && ENABLE_ANIM_DEBUG
	if (SearchResult.IsValid())
	{
		const bool bDrawMatch = CVarAnimMotionMatchDrawMatchEnable.GetValueOnAnyThread();
		const bool bDrawquery = CVarAnimMotionMatchDrawQueryEnable.GetValueOnAnyThread();

		if (bDrawMatch || bDrawquery)
		{
			TArray<FAnimInstanceProxy*, TInlineAllocator<PreallocatedRolesNum, TMemStackAllocator<>>> AnimInstanceProxies;
			AnimInstanceProxies.SetNum(Roles.Num());
			
			for (int32 RoleIndex = 0; RoleIndex < Roles.Num(); ++RoleIndex)
			{
				AnimInstanceProxies[RoleIndex] = UAnimInstanceProxyProvider::GetAnimInstanceProxy(AnimInstances[RoleIndex]);
			}

			if (bDrawMatch)
			{
				FDebugDrawParams DrawParams(AnimInstanceProxies, SearchContext.GetPoseHistories(), SearchContext.GetRoleToIndex(), SearchResult.Database.Get());
				DrawParams.DrawFeatureVector(SearchResult.PoseIdx);
			}

			if (bDrawquery)
			{
				FDebugDrawParams DrawParams(AnimInstanceProxies, SearchContext.GetPoseHistories(), SearchContext.GetRoleToIndex(), SearchResult.Database.Get(), EDebugDrawFlags::DrawQuery);
				DrawParams.DrawFeatureVector(SearchContext.GetOrBuildQuery(SearchResult.Database->Schema));
			}
		}
	}
#endif // ENABLE_DRAW_DEBUG && ENABLE_ANIM_DEBUG

#if UE_POSE_SEARCH_TRACE_ENABLED
	FMotionMatchingState MotionMatchingState;
	MotionMatchingState.CurrentSearchResult = SearchResult;
	MotionMatchingState.ElapsedPoseSearchTime = 0.0f;
	TraceMotionMatching(SearchContext, MotionMatchingState, DeltaSeconds, true, FObjectTrace::GetWorldElapsedTime(AnimInstances[0] ? AnimInstances[0]->GetWorld() : nullptr));
#endif // UE_POSE_SEARCH_TRACE_ENABLED

	return SearchResult;
}

const FAnimNode_PoseSearchHistoryCollector_Base* UPoseSearchLibrary::FindPoseHistoryNode(
	const FName PoseHistoryName,
	const UAnimInstance* AnimInstance)
{
	if (AnimInstance)
	{
		TSet<const UAnimInstance*, DefaultKeyFuncs<const UAnimInstance*>, TInlineSetAllocator<128>> AlreadyVisited;
		TArray<const UAnimInstance*, TInlineAllocator<128>> ToVisit;

		ToVisit.Add(AnimInstance);
		AlreadyVisited.Add(AnimInstance);

		while (!ToVisit.IsEmpty())
		{
			const UAnimInstance* Visiting = ToVisit.Pop();

			if (IAnimClassInterface* AnimBlueprintClass = IAnimClassInterface::GetFromClass(Visiting->GetClass()))
			{
				if (const FAnimSubsystem_Tag* TagSubsystem = AnimBlueprintClass->FindSubsystem<FAnimSubsystem_Tag>())
				{
					if (const FAnimNode_PoseSearchHistoryCollector_Base* HistoryCollector = TagSubsystem->FindNodeByTag<FAnimNode_PoseSearchHistoryCollector_Base>(PoseHistoryName, Visiting))
					{
						return HistoryCollector;
					}
				}
			}

			const USkeletalMeshComponent* SkeletalMeshComponent = Visiting->GetSkelMeshComponent();
			const TArray<UAnimInstance*>& LinkedAnimInstances = SkeletalMeshComponent->GetLinkedAnimInstances();
			for (const UAnimInstance* LinkedAnimInstance : LinkedAnimInstances)
			{
				bool bIsAlreadyInSet = false;
				AlreadyVisited.Add(LinkedAnimInstance, &bIsAlreadyInSet);
				if (!bIsAlreadyInSet)
				{
					ToVisit.Add(LinkedAnimInstance);
				}
			}
		}
	}
	return nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Begin deprecated signatures
void UPoseSearchLibrary::MotionMatch(
	const TArrayView<UAnimInstance*> AnimInstances,
	const TArrayView<const UE::PoseSearch::FRole> Roles,
	const TArrayView<const UObject*> AssetsToSearch,
	const FName PoseHistoryName,
	const FPoseSearchContinuingProperties& ContinuingProperties,
	const FPoseSearchFutureProperties& Future,
	FPoseSearchBlueprintResult& Result,
	const int32 DebugSessionUniqueIdentifier)
{
	MotionMatch(AnimInstances, Roles, AssetsToSearch, PoseHistoryName, ContinuingProperties, Future, Result);
}

UE::PoseSearch::FSearchResult UPoseSearchLibrary::MotionMatch(
	const TArrayView<UAnimInstance*> AnimInstances,
	const TArrayView<const UE::PoseSearch::FRole> Roles,
	const TArrayView<const UE::PoseSearch::IPoseHistory*> PoseHistories,
	const TArrayView<const UObject*> AssetsToSearch,
	const FPoseSearchContinuingProperties& ContinuingProperties,
	const FPoseSearchFutureProperties& Future,
	const int32 DebugSessionUniqueIdentifier)
{
	return MotionMatch(AnimInstances, Roles, PoseHistories, AssetsToSearch, ContinuingProperties, Future);
}

UE::PoseSearch::FSearchResult UPoseSearchLibrary::MotionMatch(
	const FAnimationBaseContext& Context,
	TArrayView<const UObject*> AssetsToSearch,
	const FPoseSearchContinuingProperties& ContinuingProperties)
{
	using namespace UE::PoseSearch;

	const IPoseHistory* PoseHistory = nullptr;
	if (FPoseHistoryProvider* PoseHistoryProvider = Context.GetMessage<FPoseHistoryProvider>())
	{
		PoseHistory = &PoseHistoryProvider->GetPoseHistory();
	}

	UAnimInstance* AnimInstance = Cast<UAnimInstance>(Context.AnimInstanceProxy->GetAnimInstanceObject());
	check(AnimInstance);

	return MotionMatch(MakeArrayView(&AnimInstance, 1), MakeArrayView(&DefaultRole, 1), MakeArrayView(&PoseHistory, 1),
		AssetsToSearch, ContinuingProperties, FPoseSearchFutureProperties());
}
		
UE::PoseSearch::FSearchResult UPoseSearchLibrary::MotionMatch(
	TArrayView<UAnimInstance*> AnimInstances,
	TArrayView<const UE::PoseSearch::FRole> Roles,
	TArrayView<const UE::PoseSearch::IPoseHistory*> PoseHistories,
	TArrayView<const UObject*> AssetsToSearch,
	const FPoseSearchContinuingProperties& ContinuingProperties,
	const int32 DebugSessionUniqueIdentifier,
	float DesiredPermutationTimeOffset)
{
	return MotionMatch(AnimInstances, Roles, PoseHistories, AssetsToSearch, ContinuingProperties, FPoseSearchFutureProperties());
}

// End deprecated signatures
///////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
