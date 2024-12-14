// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "PoseSearch/PoseSearchInteractionLibrary.h"
#include "PoseSearchInteractionSubsystem.generated.h"

struct FPoseSearchContinuingProperties;
class UAnimInstance;
class UPoseSearchDatabase;

namespace UE::PoseSearch
{

struct FInteractionIsland;
struct FInteractionSearchContext;

// Experimental, this feature might be removed without warning, not for production use
struct FInteractionAvailabilityEx : public FPoseSearchInteractionAvailability
{
	FInteractionAvailabilityEx(const FPoseSearchInteractionAvailability& InAvailability, FName InPoseHistoryName, const FAnimNode_PoseSearchHistoryCollector_Base* InHistoryCollector)
		: FPoseSearchInteractionAvailability(InAvailability)
		, PoseHistoryName(InPoseHistoryName)
		, HistoryCollector(InHistoryCollector)
	{
	}

	FString GetPoseHistoryName() const;
	const FAnimNode_PoseSearchHistoryCollector_Base* GetHistoryCollector(const UAnimInstance* AnimInstance) const;

private:
	FName PoseHistoryName;
	const FAnimNode_PoseSearchHistoryCollector_Base* HistoryCollector = nullptr;
};

typedef TMap<TWeakObjectPtr<UObject>, TArray<FInteractionAvailabilityEx>> FAvailabilityRequestsMap;

} // namespace UE::PoseSearch

// World subsystem accepting the publication of characters (via their AnimInstance(s)) FPoseSearchInteractionAvailability, representing the characters willingness to partecipate in an 
// interaction with other characters from the next frame forward via Query_AnyThread method.
//
// The same method will return the FPoseSearchInteractionBlueprintResult from the PREVIOUS Tick processing (categorization of FPoseSearchInteractionAvailability(s) in multiple FInteractionIsland(s)),
// to the requesting character, containing the animation to play at what time, and the assigned role to partecipate in the selected interaction within the assigned 
// FInteractionIsland

// Execution model and threading details:
/////////////////////////////////////////
// 
// - by calling UPoseSearchInteractionLibrary::MotionMatchInteraction_Pure(TArray<FPoseSearchInteractionAvailability> Availabilities, UObject* AnimInstance), characters publish their availabilities
//   to partecipate in interactions to the UPoseSearchInteractionSubsystem
// - UPoseSearchInteractionSubsystem::Tick processes those FPoseSearchInteractionAvailability(s) and creates/updates UE::PoseSearch::FInteractionIsland. For each FInteractionIsland it injects 
//   a tick prerequisite via FInteractionIsland::InjectToActor (that calls AddPrerequisite) to all the Actors in the same island.
//   NoTe: the next frame the execution will be:
//			for each island[k]
//			{
//				for each Actor[k][i]
//				{
//					Tick CharacterMovementComponent[k][i]
//				}
// 
//				Tick Island[k].PreTickFunction (that eventually generates the trajectories with all the updated CMCs)
//
//				Tick Actor[k][0].SkeletalMeshComponent (that performs the MotionMatchInteraction queries for all the involved actors via DoSearch_AnyThread)
// 
//				Tick Island[k].PostTickFunction (currently just a threading fence for the execution of all the other SkeletalMeshComponent(s))
// 
//				for each Actor[k][i]
//				{
//					if (i != 0)
//						Tick SkeletalMeshComponent[k][i] (that DoSearch_AnyThread get the cached result calculated by Tick Actor[k][0].SkeletalMeshComponent)
//				}
//			}
// - next frame UPoseSearchInteractionLibrary::MotionMatchInteraction_Pure(TArray<FPoseSearchInteractionAvailability> Availabilities, UObject* AnimInstance), with the context of all the published 
//   availabilities and created islands, will find the associated FInteractionIsland to the AnimInstance and call FInteractionIsland::DoSearch_AnyThread 
//   (via UPoseSearchInteractionSubsystem::Query_AnyThread) that will perform ALL (YES, ALL, so the bigger the island the slower the execution) the motion matching searches
//   for all the possible Actors / databases / Roles permutations, and populate FInteractionIsland::SearchResults with ALL the results for the island.
//   Ultimately the MotionMatchInteraction_Pure will return the SearchResults associated to the requesting AnimInstance with information about what animation to play
//   at what time with wich Role.

UCLASS(Experimental, Category="Animation|Pose Search")
class POSESEARCH_API UPoseSearchInteractionSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static UPoseSearchInteractionSubsystem* GetSubsystem_AnyThread(UObject* AnimInstance);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// it processes FPoseSearchInteractionAvailability(s) and creates/updates FInteractionIsland
	virtual void Tick(float DeltaSeconds) override;
	
	virtual TStatId GetStatId() const override;

	// publishing FPoseSearchInteractionAvailability(s) for the requesting character (AnimInstance) and getting the FPoseSearchInteractionBlueprintResult from the PREVIOUS Tick update
	// containing the animation to play at what time, and the assigned role to partecipate in the selected interaction
	// Either a PoseHistoryName or a HistoryCollector are required to perform the associated motion matching searches
	void Query_AnyThread(const TArrayView<const FPoseSearchInteractionAvailability> Availabilities, UObject* AnimInstance, 
		const FPoseSearchContinuingProperties& ContinuingProperties, FPoseSearchInteractionBlueprintResult& Result,
		FName PoseHistoryName, const FAnimNode_PoseSearchHistoryCollector_Base* HistoryCollector, bool bValidateResultAgainstAvailabilities);
private:
	UE::PoseSearch::FInteractionIsland& CreateIsland();
	UE::PoseSearch::FInteractionIsland& GetAvailableIsland();

	void DestroyIsland(int32 Index);
	void DestroyAllIslands();
	void UninjectAllIslands();
	bool ValidateAllIslands() const;

	void PopulateContinuingProperties(UE::PoseSearch::FInteractionSearchContext& SearchContext, float DeltaSeconds) const;
	UE::PoseSearch::FInteractionIsland* FindIsland(UObject* InAnimInstance);
	
	void DebugDraw() const;

	UE::PoseSearch::FAvailabilityRequestsMap AvailabilityRequestsMap;
	FCriticalSection AvailabilityRequestsMapMutex;

	// array of groups of characters that needs to be anaylzed together for possible interactions
	TArray<UE::PoseSearch::FInteractionIsland*> Islands;

	// critical section to retrieve the subsystem in a thread safe manner
	static FCriticalSection RetrieveSubsystemMutex;
};