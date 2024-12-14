// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/PoseSearchInteractionSubsystem.h"
#include "PoseSearch/PoseSearchInteractionIsland.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchRole.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "VisualLogger/VisualLogger.h"

namespace UE::PoseSearch
{
	FString FInteractionAvailabilityEx::GetPoseHistoryName() const
	{
		if (HistoryCollector)
		{
			return "HistoryProvider";
		}
		return PoseHistoryName.ToString();
	}
	
	const FAnimNode_PoseSearchHistoryCollector_Base* FInteractionAvailabilityEx::GetHistoryCollector(const UAnimInstance* AnimInstance) const
	{
		if (HistoryCollector)
		{
			return HistoryCollector;
		}
		return UPoseSearchLibrary::FindPoseHistoryNode(PoseHistoryName, AnimInstance);
	}

	struct FInteractionSearchContextGroup
	{
		bool Contains(const FInteractionSearchContext& SearchContext) const
		{
			for (const TWeakObjectPtr<UAnimInstance>& AnimInstance : SearchContext.AnimInstances)
			{
				if (AnimInstances.Find(AnimInstance.Get()))
				{
					return true;
				}
			}
			return false;
		}

		void Add(const FInteractionSearchContext& SearchContext, int32 SearchContextIndex)
		{
			for (const TWeakObjectPtr<UAnimInstance>& AnimInstance : SearchContext.AnimInstances)
			{
				if (AnimInstance.Get())
				{
					AnimInstances.Add(AnimInstance.Get());
				}
			}

			SearchContextsIndices.Add(SearchContextIndex);
		}

		void Merge(const FInteractionSearchContextGroup& SearchContextGroup)
		{
			for (const UAnimInstance* AnimInstance : SearchContextGroup.AnimInstances)
			{
				check(AnimInstance);
				AnimInstances.Add(AnimInstance);
			}

			for (int32 SearchContextsIndex : SearchContextGroup.SearchContextsIndices)
			{
				SearchContextsIndices.Add(SearchContextsIndex);
			}
		}

		TSet<const UAnimInstance*, DefaultKeyFuncs<const UAnimInstance*>, TInlineSetAllocator<UE::PoseSearch::PreallocatedRolesNum>> AnimInstances;
		TArray<int32, TInlineAllocator<PreallocatedSearchesNum>> SearchContextsIndices;
	};

	struct FRoledAnimInstance
	{
		UAnimInstance* AnimInstance = nullptr;
		UE::PoseSearch::FRole Role = UE::PoseSearch::DefaultRole;
		const FAnimNode_PoseSearchHistoryCollector_Base* HistoryCollector = nullptr;
		float BroadPhaseRadius = 0.f;
		float MaxCost = MAX_flt;
	};

	// @todo: inherit from a TSet if it gets too slow 
	// sorted array of FRoledAnimInstance(s). Sorted by UAnimInstance to prevent "alternating" tuples between different frames like
	// ([AnimInstanceA, RoleA], [AnimInstanceB, RoleB]) vs ([AnimInstanceB, RoleB], [AnimInstanceA, RoleA]) 
	// that ultimately represent the same search, but endign up with different SearchIDs
	struct FRoledAnimInstances
	{
		void AddRoledAnimInstace(UAnimInstance* AnimInstance, UE::PoseSearch::FRole Role, const FAnimNode_PoseSearchHistoryCollector_Base* HistoryCollector, float BroadPhaseRadius, float MaxCost)
		{
			check(AnimInstance && HistoryCollector);

			for (FRoledAnimInstance& RoledAnimInstance : RoledAnimInstances)
			{
				if (RoledAnimInstance.AnimInstance == AnimInstance && RoledAnimInstance.HistoryCollector == HistoryCollector && RoledAnimInstance.Role == Role)
				{
					// we already found the FRoledAnimInstances. Updating the BroadPhaseRadius and MaxCost accordingly
					RoledAnimInstance.BroadPhaseRadius = FMath::Max(RoledAnimInstance.BroadPhaseRadius, BroadPhaseRadius);
					
					// MaxCost is valid only if greater than zero
					if (MaxCost > 0.f)
					{
						RoledAnimInstance.MaxCost = FMath::Min(RoledAnimInstance.MaxCost, MaxCost);
					}
					return;
				}
			}

			// we couldn't find the FRoledAnimInstances. Let's add a new one
			FRoledAnimInstance& NewRoledAnimInstance = RoledAnimInstances.AddDefaulted_GetRef();
			NewRoledAnimInstance.AnimInstance = AnimInstance;
			NewRoledAnimInstance.HistoryCollector = HistoryCollector;
			NewRoledAnimInstance.Role = Role;
			NewRoledAnimInstance.BroadPhaseRadius = BroadPhaseRadius;
			if (MaxCost > 0.f)
			{
				NewRoledAnimInstance.MaxCost = MaxCost;
			}

			// @todo: use a better datastructure to keep things FRoledAnimInstance(s) sorted? Or use insertion sort?
			RoledAnimInstances.Sort([](const FRoledAnimInstance& A, const FRoledAnimInstance& B)
			{
				// we're sorting by AnimInstance address in memory, since searches neeed to be consistent 
				// between frames on the same machine, not between replicated machines
				return B.AnimInstance < A.AnimInstance;
			});
		}

		TArrayView<const FRoledAnimInstance> GetDataView() const
		{
			return MakeArrayView(RoledAnimInstances.GetData(), RoledAnimInstances.Num());
		}

	private:
		TArray<FRoledAnimInstance, TInlineAllocator<UE::PoseSearch::PreallocatedRolesNum, TMemStackAllocator<>>> RoledAnimInstances;
	};

	typedef TMap<const UPoseSearchDatabase*, FRoledAnimInstances, TInlineSetAllocator<PreallocatedSearchesNum, UE::PoseSearch::TMemStackSetAllocator<>>> FDatabaseToRoledAnimInstances;
	typedef TPair<const UPoseSearchDatabase*, FRoledAnimInstances> FDatabaseToRoledAnimInstancesPair;

	static void ProcessAvailabilityRequestsMap(const FAvailabilityRequestsMap& AvailabilityRequestsMap, UWorld* SubsystemWorld, FDatabaseToRoledAnimInstances& DatabaseToRoledAnimInstances)
	{
		for (FAvailabilityRequestsMap::TConstIterator Iter = AvailabilityRequestsMap.CreateConstIterator(); Iter; ++Iter)
		{
			// making sure the weak pointer is still valid
			UAnimInstance* AnimInstance = Cast<UAnimInstance>(Iter->Key.Get());
			if (!AnimInstance)
			{
				UE_LOG(LogPoseSearch, Log, TEXT("ProcessAvailabilityRequestsMap null anim instance. The associated character got removed from the world"));
				continue;
			}

			if (!AnimInstance->GetWorld())
			{
				UE_LOG(LogPoseSearch, Error, TEXT("ProcessAvailabilityRequestsMap AnimInstance %s is not in a world!"), *AnimInstance->GetName());
				continue;
			}

			if (SubsystemWorld != AnimInstance->GetWorld())
			{
				UE_LOG(LogPoseSearch, Error, TEXT("ProcessAvailabilityRequestsMap AnimInstance %s is from World %s, and supposed to be in World @s!"), *AnimInstance->GetName(), *AnimInstance->GetWorld()->GetName(), *SubsystemWorld->GetName());
				continue;
			}

			for (const FInteractionAvailabilityEx& AvailabilityRequest : Iter->Value)
			{
				if (!AvailabilityRequest.Database)
				{
					UE_LOG(LogPoseSearch, Log, TEXT("ProcessAvailabilityRequestsMap null AvailabilityRequest.Database"));
					continue;
				}

				if (!AvailabilityRequest.Database->Schema)
				{
					UE_LOG(LogPoseSearch, Error, TEXT("ProcessAvailabilityRequestsMap null Schema for Database %s"), *AvailabilityRequest.Database->GetName());
					continue;
				}

				const FAnimNode_PoseSearchHistoryCollector_Base* HistoryCollector = AvailabilityRequest.GetHistoryCollector(AnimInstance);
				if (!HistoryCollector)
				{
					UE_LOG(LogPoseSearch, Error, TEXT("ProcessAvailabilityRequestsMap couldn't find PoseHistory %s for AnimInstance %s"), *AvailabilityRequest.GetPoseHistoryName(), *AnimInstance->GetName());
					continue;
				}

				FRoledAnimInstances& RoledAnimInstances = DatabaseToRoledAnimInstances.FindOrAdd(AvailabilityRequest.Database);
				if (AvailabilityRequest.RolesFilter.IsEmpty())
				{
					// adding ALL the possible roles from the database:
					for (const FPoseSearchRoledSkeleton& RoledSkeleton : AvailabilityRequest.Database->Schema->GetRoledSkeletons())
					{
						RoledAnimInstances.AddRoledAnimInstace(AnimInstance, RoledSkeleton.Role, HistoryCollector, AvailabilityRequest.BroadPhaseRadius, AvailabilityRequest.MaxCost);
					}
				}
				else
				{
					for (const UE::PoseSearch::FRole& Role : AvailabilityRequest.RolesFilter)
					{
						if (AvailabilityRequest.Database->Schema->GetRoledSkeleton(Role))
						{
							RoledAnimInstances.AddRoledAnimInstace(AnimInstance, Role, HistoryCollector, AvailabilityRequest.BroadPhaseRadius, AvailabilityRequest.MaxCost);
						}
						else
						{
							UE_LOG(LogPoseSearch, Warning, TEXT("ProcessAvailabilityRequestsMap unsupported Role %s for Database %s"), *Role.ToString(), *AvailabilityRequest.Database->GetName());
						}
					}
				}
			}
		}
	}

	template <typename DataType, typename EvaluateCombinationType>
	static void GeneratePermutationsRecursive(const TArrayView<DataType> Data, int32 DataIndex, TArrayView<int32> Combination, int32 CombinationIndex, EvaluateCombinationType EvaluateCombination)
	{
		if (CombinationIndex == Combination.Num())
		{ 
			EvaluateCombination(Data, Combination);
		}
		else if (DataIndex < Data.Num())
		{
			Combination[CombinationIndex] = DataIndex;
			GeneratePermutationsRecursive(Data, DataIndex + 1, Combination, CombinationIndex + 1, EvaluateCombination);
			GeneratePermutationsRecursive(Data, DataIndex + 1, Combination, CombinationIndex, EvaluateCombination);
		}
	} 

	template <typename DataType, typename EvaluateCombinationType>
	static void GeneratePermutations(const TArrayView<DataType> Data, int32 CombinationCardinality, EvaluateCombinationType EvaluateCombination)
	{
		TArray<int32, TInlineAllocator<UE::PoseSearch::PreallocatedRolesNum>> Combination;
		Combination.SetNum(CombinationCardinality);
		GeneratePermutationsRecursive(Data, 0, Combination, 0, EvaluateCombination);
	}
}

FCriticalSection UPoseSearchInteractionSubsystem::RetrieveSubsystemMutex;

UE::PoseSearch::FInteractionIsland& UPoseSearchInteractionSubsystem::CreateIsland()
{
	return *Islands.Add_GetRef(new UE::PoseSearch::FInteractionIsland(ToRawPtr(GetWorld()->PersistentLevel)));
}

void UPoseSearchInteractionSubsystem::DestroyIsland(int32 Index)
{
	delete Islands[Index];
	Islands.RemoveAt(Index);
}

UE::PoseSearch::FInteractionIsland& UPoseSearchInteractionSubsystem::GetAvailableIsland()
{
	using namespace UE::PoseSearch;

	for (FInteractionIsland* Island : Islands)
	{
		if (Island->IsUninjected())
		{
			return *Island;
		}
	}

	return CreateIsland();
}

void UPoseSearchInteractionSubsystem::DestroyAllIslands()
{
	for (int32 IslandIndex = Islands.Num() - 1; IslandIndex >= 0; --IslandIndex)
	{
		DestroyIsland(IslandIndex);
	}
}

void UPoseSearchInteractionSubsystem::UninjectAllIslands()
{
	using namespace UE::PoseSearch;

	check(IsInGameThread());

	for (FInteractionIsland* Island : Islands)
	{
		Island->Uninject();
	}
}

bool UPoseSearchInteractionSubsystem::ValidateAllIslands() const
{
#if DO_CHECK
	using namespace UE::PoseSearch;

	TSet<TWeakObjectPtr<UCharacterMovementComponent>> CharacterMovementComponents;
	TSet<TWeakObjectPtr<USkeletalMeshComponent>> SkeletalMeshComponents;

	bool bAlreadyInSet = false;
	for (const FInteractionIsland* Island : Islands)
	{
		for (const TWeakObjectPtr<UCharacterMovementComponent>& CharacterMovementComponent : Island->GetCharacterMovementComponents())
		{
			CharacterMovementComponents.Add(CharacterMovementComponent, &bAlreadyInSet);
			if (bAlreadyInSet)
			{
				return false;
			}
		}

		for (const TWeakObjectPtr<USkeletalMeshComponent>& SkeletalMeshComponent : Island->GetSkeletalMeshComponents())
		{
			SkeletalMeshComponents.Add(SkeletalMeshComponent, &bAlreadyInSet);
			if (bAlreadyInSet)
			{
				return false;
			}
		}
	}
#endif // DO_CHECK
	return true;
}

void UPoseSearchInteractionSubsystem::PopulateContinuingProperties(UE::PoseSearch::FInteractionSearchContext& SearchContext, float DeltaSeconds) const
{
	using namespace UE::PoseSearch;

	check(IsInGameThread());

	// searching this SearchContext in all the islands to initialize its continuing pose
	for (const FInteractionIsland* Island : Islands)
	{
		if (const FSearchResult* SearchResult = Island->FindSearchResult(SearchContext))
		{
			// is still valid...
			if (SearchResult->IsValid())
			{
				if (const UE::PoseSearch::FSearchIndexAsset* SearchIndexAsset = SearchResult->GetSearchIndexAsset())
				{
					if (const FPoseSearchDatabaseAnimationAssetBase* DatabaseAsset = SearchResult->Database->GetDatabaseAnimationAsset<FPoseSearchDatabaseAnimationAssetBase>(*SearchIndexAsset))
					{
						SearchContext.ContinuingProperties.PlayingAsset = DatabaseAsset->GetAnimationAsset();
						SearchContext.ContinuingProperties.PlayingAssetAccumulatedTime = SearchResult->AssetTime + DeltaSeconds;
					}
				}
			}
			break;
		}
	}
}

UE::PoseSearch::FInteractionIsland* UPoseSearchInteractionSubsystem::FindIsland(UObject* InAnimInstance)
{
	using namespace UE::PoseSearch;

	if (InAnimInstance)
	{
		if (UAnimInstance* AnimInstance = Cast<UAnimInstance>(InAnimInstance))
		{
			if (AActor* Actor = AnimInstance->GetOwningActor())
			{
				if (USkeletalMeshComponent* SkeletalMeshComponent = Actor->GetComponentByClass<USkeletalMeshComponent>())
				{
					for (FInteractionIsland* Island : Islands)
					{
						for (const TWeakObjectPtr<USkeletalMeshComponent>& IslandSkeletalMeshComponent : Island->GetSkeletalMeshComponents())
						{
							if (IslandSkeletalMeshComponent.Get() == SkeletalMeshComponent)
							{
								return Island;
							}
						}
					}
				}
			}
		}
	}
	return nullptr;
}

UPoseSearchInteractionSubsystem* UPoseSearchInteractionSubsystem::GetSubsystem_AnyThread(UObject* AnimInstance)
{
	if (AnimInstance)
	{
		if (UWorld* World = AnimInstance->GetWorld())
		{
			FScopeLock Lock(&RetrieveSubsystemMutex);

			// don't create a subsystem from any thread
			if (World->HasSubsystem<UPoseSearchInteractionSubsystem>())
			{
				return World->GetSubsystem<UPoseSearchInteractionSubsystem>();
			}
		}
	}
	return nullptr;
}

void UPoseSearchInteractionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UPoseSearchInteractionSubsystem::Deinitialize()
{
	DestroyAllIslands();

	Super::Deinitialize();
}

void UPoseSearchInteractionSubsystem::Tick(float DeltaSeconds)
{
	using namespace UE::PoseSearch;

	Super::Tick(DeltaSeconds);

	if (AvailabilityRequestsMap.IsEmpty() && Islands.IsEmpty())
	{
		// nothing to do. early out
		return;
	}

	check(IsInGameThread());
	check(GetWorld());

	// FScopeLock Lock(&AvailabilityRequestsMapMutex); is not necessary since UPoseSearchInteractionSubsystem gets ticked outside the parallel animation jobs
	FMemMark Mark(FMemStack::Get());
	
	// processing the AvailabilityRequestsMap to fill up a map of Databases pointing to an array of all the AnimInstances with related roles
	FDatabaseToRoledAnimInstances DatabaseToRoledAnimInstances;
	ProcessAvailabilityRequestsMap(AvailabilityRequestsMap, GetWorld(), DatabaseToRoledAnimInstances);

	// for each database now we try to create all the possible permutations of the roled anim instances
	// for example, given a database set up with assets for 2 characters interactions with roles RoleA and RoleB
	// and 2 anim instances, all of them willing to partecipate in the 2 characters interaction with both roles RoleA and RoleB:
	// CharA could be taking RoleA and RoleB,
	// CharB could be taking RoleA and RoleB,
	// we generate all the permutations from the array of options:
	// CharA/RoleA, CharA/RoleB, CharB/RoleA, CharB/RoleB
	//
	// and we prune the invalid tuples:
	//
	// CharA/RoleA - CharA/RoleB -> invalid because of same CharA
	// CharA/RoleA - CharB/RoleA -> invalid because of same RoleA
	// CharA/RoleA - CharB/RoleB -> VALID!
	//
	// CharA/RoleB - CharB/RoleA -> VALID!
	// CharA/RoleB - CharB/RoleB -> invalid because of same RoleB
	//
	// CharB/RoleA - CharB/RoleB -> invalid because of same CharB

	TSet<const UAnimInstance*, DefaultKeyFuncs<const UAnimInstance*>, TInlineSetAllocator<UE::PoseSearch::PreallocatedRolesNum>> AnimInstances;
	TSet<UE::PoseSearch::FRole, DefaultKeyFuncs<UE::PoseSearch::FRole>, TInlineSetAllocator<UE::PoseSearch::PreallocatedRolesNum>> CoveredRoles;
	TArray<FInteractionSearchContext, TInlineAllocator<PreallocatedSearchesNum, TMemStackAllocator<>>> SearchContexts;
	for (const FDatabaseToRoledAnimInstancesPair& DatabaseToRoledAnimInstancesPair : DatabaseToRoledAnimInstances)
	{
		const UPoseSearchDatabase* Database = DatabaseToRoledAnimInstancesPair.Key;
		const TArray<FPoseSearchRoledSkeleton>& RoledSkeletons = Database->Schema->GetRoledSkeletons();
		const int32 CombinationCardinality = RoledSkeletons.Num();
		
		// @todo: naive implementation: optimize this code!
		GeneratePermutations(DatabaseToRoledAnimInstancesPair.Value.GetDataView(), CombinationCardinality, 
			[&AnimInstances, &CoveredRoles, Database, &RoledSkeletons, &SearchContexts](const TArrayView<const FRoledAnimInstance> RoledAnimInstances, const TArrayView<int32> Combination)
			{
				// checking if this combination is valid for the Database:
				AnimInstances.Reset();
				CoveredRoles.Reset();

				for (int32 CombinationIndex = 0; CombinationIndex < Combination.Num(); ++CombinationIndex)
				{
					const FRoledAnimInstance& RoledAnimInstance = RoledAnimInstances[Combination[CombinationIndex]];

					bool bIsAlreadyInSet = false;
					AnimInstances.Add(RoledAnimInstance.AnimInstance, &bIsAlreadyInSet);

					// is there any duplicated AnimInstance?
					if (bIsAlreadyInSet)
					{
						return false;
					}

					CoveredRoles.Add(RoledAnimInstance.Role);
				}

				// does it cover all the roles?
				for (const FPoseSearchRoledSkeleton& RoledSkeleton : RoledSkeletons)
				{
					if (!CoveredRoles.Find(RoledSkeleton.Role))
					{
						return false;
					}
				}

				// @todo: naive implementation: optimize this code!
				// checking if all the actors are within BroadPhaseRadius to each other
				for (int32 CombinationIndexA = 0; CombinationIndexA < Combination.Num(); ++CombinationIndexA)
				{
					const FRoledAnimInstance& RoledAnimInstanceA = RoledAnimInstances[Combination[CombinationIndexA]];
					const FVector ActorLocationA = RoledAnimInstanceA.AnimInstance->GetOwningActor()->GetActorLocation();

					for (int32 CombinationIndexB = 0; CombinationIndexB < Combination.Num(); ++CombinationIndexB)
					{
						if (CombinationIndexA != CombinationIndexB)
						{
							const FRoledAnimInstance& RoledAnimInstanceB = RoledAnimInstances[Combination[CombinationIndexB]];
							const FVector ActorLocationB = RoledAnimInstanceB.AnimInstance->GetOwningActor()->GetActorLocation();

							const FVector DeltaLocation = ActorLocationA - ActorLocationB;
							if (DeltaLocation.Length() > RoledAnimInstanceA.BroadPhaseRadius)
							{
								return false;
							}
						}
					}
				}

				FInteractionSearchContext& SearchContext = SearchContexts.AddDefaulted_GetRef();
				SearchContext.Database = Database;
				SearchContext.AnimInstances.Reserve(Combination.Num());
				SearchContext.HistoryCollectors.Reserve(Combination.Num());
				SearchContext.Roles.Reserve(Combination.Num());
				
#if ENABLE_DRAW_DEBUG
				SearchContext.BroadPhaseRadiuses.Reserve(Combination.Num());
#endif //ENABLE_DRAW_DEBUG

				for (int32 CombinationIndex = 0; CombinationIndex < Combination.Num(); ++CombinationIndex)
				{
					const FRoledAnimInstance& RoledAnimInstance = RoledAnimInstances[Combination[CombinationIndex]];
					SearchContext.AnimInstances.Add(RoledAnimInstance.AnimInstance);
					SearchContext.HistoryCollectors.Add(RoledAnimInstance.HistoryCollector);
					SearchContext.Roles.Add(RoledAnimInstance.Role);
#if ENABLE_DRAW_DEBUG
					SearchContext.BroadPhaseRadiuses.Add(RoledAnimInstance.BroadPhaseRadius);
#endif //ENABLE_DRAW_DEBUG

					// search is valid only if cost result is lower than any AvailabilityRequest.MaxCost (copied in RoledAnimInstance.MaxCost)
					SearchContext.MaxCost = FMath::Min(SearchContext.MaxCost, RoledAnimInstance.MaxCost);
				}

				return true;
			});
	}
	
	// for each valid SearchContexts we try to figure out the continuing pose properties from the current Islands
	for (FInteractionSearchContext& SearchContext : SearchContexts)
	{
		PopulateContinuingProperties(SearchContext, DeltaSeconds);
	}

	// grouping SearchContexts in different Islands
	TArray<FInteractionSearchContextGroup, TInlineAllocator<PreallocatedSearchesNum, TMemStackAllocator<>>> SearchContextGroups;
	for (int32 SearchContextIndex = 0; SearchContextIndex < SearchContexts.Num(); ++SearchContextIndex)
	{
		const FInteractionSearchContext& SearchContext = SearchContexts[SearchContextIndex];

		int32 MainSearchContextGroupIndex = INDEX_NONE;
		for (int32 SearchContextGroupIndex = 0; SearchContextGroupIndex < SearchContextGroups.Num();)
		{
			if (SearchContextGroups[SearchContextGroupIndex].Contains(SearchContext))
			{
				if (MainSearchContextGroupIndex == INDEX_NONE)
				{
					MainSearchContextGroupIndex = SearchContextGroupIndex;
					SearchContextGroups[MainSearchContextGroupIndex].Add(SearchContext, SearchContextIndex);
					++SearchContextGroupIndex;
				}
				else
				{
					SearchContextGroups[MainSearchContextGroupIndex].Merge(SearchContextGroups[SearchContextGroupIndex]);
					SearchContextGroups.RemoveAt(SearchContextGroupIndex);
				}
			}
			else
			{
				++SearchContextGroupIndex;
			}
		}
		if (MainSearchContextGroupIndex == INDEX_NONE)
		{
			SearchContextGroups.AddDefaulted_GetRef().Add(SearchContext, SearchContextIndex);
		}
	}

#if ENABLE_DRAW_DEBUG
	DebugDraw();
#endif //ENABLE_DRAW_DEBUG

	UninjectAllIslands();

	for (const FInteractionSearchContextGroup& SearchContextGroup : SearchContextGroups)
	{
		UE::PoseSearch::FInteractionIsland& Island = GetAvailableIsland();

		for (const UAnimInstance* AnimInstance : SearchContextGroup.AnimInstances)
		{
			Island.InjectToActor(AnimInstance->GetOwningActor());
		}

		for (int32 SearchContextsIndex : SearchContextGroup.SearchContextsIndices)
		{
			Island.AddSearchContext(SearchContexts[SearchContextsIndex]);
		}
	}

	AvailabilityRequestsMap.Reset();

	check(ValidateAllIslands());
}

TStatId UPoseSearchInteractionSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UPoseSearchInteractionSubsystem, STATGROUP_Tickables);
}

void UPoseSearchInteractionSubsystem::Query_AnyThread(const TArrayView<const FPoseSearchInteractionAvailability> Availabilities, UObject* AnimInstance, 
	const FPoseSearchContinuingProperties& ContinuingProperties, FPoseSearchInteractionBlueprintResult& Result,
	FName PoseHistoryName, const FAnimNode_PoseSearchHistoryCollector_Base* HistoryCollector, bool bValidateResultAgainstAvailabilities)
{
	using namespace UE::PoseSearch;

	Result = FPoseSearchInteractionBlueprintResult();

	if (AnimInstance)
	{
		// if we find AnimInstance in an island, we perform ALL the Island motion matching searches.
		// thread safety is ensured by the lock on Island->SearchResultsMutex in DoSearch_AnyThread
		if (FInteractionIsland* Island = FindIsland(AnimInstance))
		{
			Island->DoSearch_AnyThread(AnimInstance, ContinuingProperties, Result);

			if (bValidateResultAgainstAvailabilities && Result.SelectedAnimation)
			{
				bool bResultValidated = false;

				for (const FPoseSearchInteractionAvailability& Availability : Availabilities)
				{
					if (Availability.Database == Result.SelectedDatabase &&
						(Availability.RolesFilter.IsEmpty() || Availability.RolesFilter.Contains(Result.Role)))
					{
						bResultValidated = true;
						break;
					}
				}

				if (!bResultValidated)
				{
					Result = FPoseSearchInteractionBlueprintResult();
				}
			}
		}

		// queuing the availabilities for the next frame Query_AnyThread
		if (!Availabilities.IsEmpty())
		{
			FScopeLock Lock(&AvailabilityRequestsMapMutex);

			TArray<FInteractionAvailabilityEx>& AvailabilityRequests = AvailabilityRequestsMap.FindOrAdd(AnimInstance);
			for (const FPoseSearchInteractionAvailability& Availability : Availabilities)
			{
				if (Availability.Database)
				{
					AvailabilityRequests.Emplace(Availability, PoseHistoryName, HistoryCollector);
				}
			}
		}
	}
}

void UPoseSearchInteractionSubsystem::DebugDraw() const
{
	using namespace UE::PoseSearch;

#if ENABLE_DRAW_DEBUG
	static const FColor Colors[] = { FColor::Red, FColor::Green, FColor::Blue, FColor::Yellow, FColor::Black };
	static const int32 NumColors = sizeof(Colors) / sizeof(Colors[0]);
	int32 CurrentColorIndex = 0;

	for (const FInteractionIsland* Island : Islands)
	{
		if (!Island->IsUninjected())
		{
			Island->DebugDraw(Colors[CurrentColorIndex]);
			CurrentColorIndex = (CurrentColorIndex + 1) % NumColors;
		}
	}

#endif //ENABLE_DRAW_DEBUG

#if ENABLE_VISUAL_LOG
	TStringBuilder<512> StringBuilder;

	for (FAvailabilityRequestsMap::TConstIterator Iter = AvailabilityRequestsMap.CreateConstIterator(); Iter; ++Iter)
	{
		// making sure the weak pointer is still valid
		if (UAnimInstance* AnimInstance = Cast<UAnimInstance>(Iter->Key.Get()))
		{
			// looking for valid results for this AnimInstance
			const UPoseSearchDatabase* ResultDatabase = nullptr;
			UE::PoseSearch::FRole ResultRole;
			for (const FInteractionIsland* Island : Islands)
			{
				if (!Island->IsUninjected())
				{
					for (const FInteractionSearchResult& SearchResult : Island->GetSearchResults())
					{
						const FInteractionSearchContext& SearchContext = Island->GetSearchContexts()[SearchResult.SearchIndex];
						for (int32 AnimInstanceIndex = 0; AnimInstanceIndex < SearchContext.AnimInstances.Num(); ++AnimInstanceIndex)
						{
							if (SearchContext.AnimInstances[AnimInstanceIndex].Get() == AnimInstance)
							{
								ResultDatabase = SearchResult.Database.Get();
								check(ResultDatabase);
								ResultRole = SearchContext.Roles[AnimInstanceIndex];
								break;
							}
						}

						if (ResultDatabase)
						{
							break;
						}
					}
				}

				if (ResultDatabase)
				{
					break;
				}
			}

			StringBuilder.Reset();
			for (const FInteractionAvailabilityEx& AvailabilityRequest : Iter->Value)
			{
				StringBuilder.Append(GetNameSafe(AvailabilityRequest.Database));
				StringBuilder.Append(" / ");
				StringBuilder.Append(AvailabilityRequest.GetPoseHistoryName());
				StringBuilder.Append(" [");
				bool bAddComma = false;

				if (AvailabilityRequest.RolesFilter.IsEmpty())
				{
					// adding ALL the possible roles from the database:
					if (AvailabilityRequest.Database && AvailabilityRequest.Database->Schema)
					{
						for (const FPoseSearchRoledSkeleton& RoledSkeleton : AvailabilityRequest.Database->Schema->GetRoledSkeletons())
						{
							if (bAddComma)
							{
								StringBuilder.Append(",");
							}
							else
							{
								bAddComma = true;
							}

							if (AvailabilityRequest.Database == ResultDatabase && RoledSkeleton.Role == ResultRole)
							{
								StringBuilder.Append("(*)");
							}

							StringBuilder.Append(RoledSkeleton.Role.ToString());
						}
					}
				}
				else
				{
					for (const FName& Role : AvailabilityRequest.RolesFilter)
					{
						if (bAddComma)
						{
							StringBuilder.Append(",");
						}
						else
						{
							bAddComma = true;
						}

						if (AvailabilityRequest.Database == ResultDatabase && Role == ResultRole)
						{
							StringBuilder.Append("(*)");
						}

						StringBuilder.Append(Role.ToString());
					}
				}

				StringBuilder.Append("]");
				StringBuilder.Append("\n");
			}
			
			// @todo: waiting for visual logger fix to display strings :/
			const FVector StringOffset(0.f, 0.f, 0.001f);
			const FVector ActorLocation = AnimInstance->GetOwningActor()->GetActorLocation();
			UE_VLOG_SEGMENT(AnimInstance, "PoseSearchInteraction", Display, ActorLocation, ActorLocation + StringOffset, FColor::Transparent, TEXT("%s"), StringBuilder.ToString());
		}
	}
#endif // ENABLE_VISUAL_LOG
}