// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PoseSearch/PoseSearchLibrary.h"

struct FPoseSearchInteractionBlueprintResult;
struct FPoseSearchContinuingProperties;
class UCharacterMovementComponent;

namespace UE::PoseSearch
{
struct FInteractionIsland;

// Experimental, this feature might be removed without warning, not for production use
struct FInteractionSearchContext
{
	// @todo: since AnimInstances, HistoryCollectors, Roles, and BroadPhaseRadiuses have the same cardinality,
	// add an array of structs instead of many arrays
	TArray<TWeakObjectPtr<UAnimInstance>> AnimInstances;
	// @todo: perhaps use the animnode of the pose history collector instead
	TArray<const FAnimNode_PoseSearchHistoryCollector_Base*> HistoryCollectors;
	TArray<UE::PoseSearch::FRole> Roles;

#if ENABLE_DRAW_DEBUG
	TArray<float> BroadPhaseRadiuses;
#endif // ENABLE_DRAW_DEBUG

	float MaxCost = MAX_flt;

	TWeakObjectPtr<const UPoseSearchDatabase> Database;
	FPoseSearchContinuingProperties ContinuingProperties;

	bool IsValid() const;
	bool IsEquivalent(const FInteractionSearchContext& Other) const;
};

// Experimental, this feature might be removed without warning, not for production use
struct FInteractionSearchResult : public FSearchResult
{
	int32 SearchIndex = INDEX_NONE;
	TArray<FTransform, TInlineAllocator<PreallocatedRolesNum>> FullAlignedActorRootBoneTransforms;

	bool operator==(const FInteractionSearchResult& Other) const;
};

// Experimental, this feature might be removed without warning, not for production use
// FInteractionIsland contains ticks functions injected between the interacting actors UCharacterMovementComponent and USkeletalMeshComponent
// to create a execution threading fence to be able to perform motion matching searches between the involved characters in a thread safe manner.
// Look at UPoseSearchInteractionSubsystem "Execution model and threading details" for additional information
struct FInteractionIsland
{
	UE_NONCOPYABLE(FInteractionIsland);
	
	FInteractionIsland(ULevel* Level);
	~FInteractionIsland();

	bool DoSearch_AnyThread(UObject* AnimInstance, const FPoseSearchContinuingProperties& ContinuingProperties, FPoseSearchInteractionBlueprintResult& Result);

	const TArray<TWeakObjectPtr<UCharacterMovementComponent>>& GetCharacterMovementComponents() const { return CharacterMovementComponents; }
	const TArray<TWeakObjectPtr<USkeletalMeshComponent>>& GetSkeletalMeshComponents() const { return SkeletalMeshComponents; }
	const TArray<FInteractionSearchContext>& GetSearchContexts() const { return SearchContexts; }
	const TArray<FInteractionSearchResult>& GetSearchResults() const { return SearchResults; }

	bool IsUninjected() const;
	void InjectToActor(AActor* Actor);
	void Uninject();

	const FInteractionSearchResult* FindSearchResult(const FInteractionSearchContext& SearchContext) const;
	void AddSearchContext(const FInteractionSearchContext& SearchContext);

	void DebugDraw(const FColor& Color = FColor::Red) const;

private:
	bool GetResult_AnyThread(UObject* AnimInstance, FPoseSearchInteractionBlueprintResult& Result);

	struct FPreTickFunction : public FTickFunction
	{
		virtual void ExecuteTick(float DeltaTime, enum ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent) override;
		virtual FString DiagnosticMessage() override { return TEXT("FPreTickFunction"); }
		FInteractionIsland* Island = nullptr;
	};
	FPreTickFunction PreTickFunction;

	struct FPostTickFunction : public FTickFunction
	{
		virtual void ExecuteTick(float DeltaTime, enum ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent) override;
		virtual FString DiagnosticMessage() override { return TEXT("FPostTickFunction"); }
	};
	FPostTickFunction PostTickFunction;

	TArray<TWeakObjectPtr<UCharacterMovementComponent>> CharacterMovementComponents;
	TArray<TWeakObjectPtr<USkeletalMeshComponent>> SkeletalMeshComponents;
	
	// there's one FSearchContext for each search we need to perform (including all the possible roles permutations). Added by UPoseSearchInteractionSubsystem::Tick
	TArray<FInteractionSearchContext> SearchContexts;

	// SearchResults contains only the best results, and it has not necessarly the same cardinality as SearchContexts. usually SearchResults.Num() < SearchContexts.Num()
	TArray<FInteractionSearchResult> SearchResults;
	bool bSearchPerfomed = false;

	FCriticalSection SearchResultsMutex;
};

} // namespace UE::PoseSearch
