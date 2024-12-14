// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "PoseSearch/PoseSearchLibrary.h"
#include "PoseSearchInteractionLibrary.generated.h"

// subsystem in charge of coordinating Characters availability for motion matched interactions,
// scheduling motion matching searches and synchronizing animations playback


// input for MotionMatchInteraction_Pure: it declares that the associated character (AnimInstance) is willing to partecipate in an interction 
// described by a UMultiAnimAsset (derived by UPoseSearchInteractionAsset) contained in the UPoseSearchDatabase Database
// with one of the roles in RolesFilter (if empty ANY of the Database roles can be taken)
// the MotionMatchInteraction_Pure will ultimately setup a motion matching query using looking for the pose history "PoseHistoryName" 
// to gather bone and trajectory positions for this character
// for an interaction to be valid, the query needs to find all the other interacting characters within BroadPhaseRadius,
// and reach a maximum cost of MaxCost
USTRUCT(Experimental, BlueprintType, Category = "Animation|Pose Search")
struct POSESEARCH_API FPoseSearchInteractionAvailability
{
	GENERATED_BODY()

	// Database describing the interaction. It'll contains multi character UMultiAnimAsset and a schema with multiple skeletons with associated roles
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings)
	TObjectPtr<UPoseSearchDatabase> Database;

	// roles the character is willing to take to partecipate in this interaction. If empty ANY of the Database roles can be taken
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings)
	TArray<FName> RolesFilter;

	// the associated character to this FPoseSearchInteractionAvailability will partecipate in an interaction only if all the necessary roles gest assigned to character within BroadPhaseRadius centimeters
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings)
	float BroadPhaseRadius = 500.f;

	// if MaxCost if greater than zero, the associated character to this FPoseSearchInteractionAvailability will not partecipate in an interaction if the motion matching search cost result is higher than MaxCost
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings)
	float MaxCost = 0.f;
};

USTRUCT(Experimental, BlueprintType, Category="Animation|Pose Search")
struct POSESEARCH_API FPoseSearchInteractionBlueprintResult
{
	GENERATED_BODY()
public:

	// animation assigned to this character to partecipate in the interaction
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category=State)
	TObjectPtr<UObject> SelectedAnimation = nullptr;
	
	// SelectedAnimation associated time
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category=State)
	float SelectedTime = 0.f;
	
	// SelectedAnimation at SelectedTime is from the continuing pose search
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category=State)
	bool bIsContinuingPoseSearch = false;

	// SelectedAnimation associated play rate
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category=State)
	float WantedPlayRate = 0.f;

	// SelectedAnimation associated looping state
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category=State)
	bool bLoop = false;
	
	// SelectedAnimation associated mirror state
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category=State)
	bool bIsMirrored = false;
	
	// SelectedAnimation associated BlendParameters (if SelectedAnimation is a UBlendSpace)
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category=State)
	FVector BlendParameters = FVector::ZeroVector;

	// selected SelectedDatabase for this character interaction
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category=State)
	TWeakObjectPtr<const UPoseSearchDatabase> SelectedDatabase = nullptr;

	// associated motion matching search cost for this result
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category=State)
	float SearchCost = MAX_flt;

	// assigned role to this character (AnimInstance)
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category=State)
	FName Role;

	// root bone transform for the character at full aligment
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category=State)
	FTransform FullAlignedActorRootBoneTransform = FTransform::Identity;	
};

UCLASS(Experimental)
class UPoseSearchInteractionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	// function publishing this character (via its AnimInstance) FPoseSearchInteractionAvailability to the UPoseSearchInteractionSubsystem,
	// FPoseSearchInteractionAvailability represents the character availability to partecipate in an interaction with other characters for the next frame.
	// that means there will always be one frame delay between publiching availabilities and getting a result back from MotionMatchInteraction_Pure!
	// 
	// if FPoseSearchInteractionBlueprintResult has a valid SelectedAnimation, this will be the animation assigned to this character to partecipate in this interaction.
	// additional interaction properties, like assigned role, SelectedAnimation time, SearchCost, etc can be found within the result
	// ContinuingProperties are used to figure out the continuing pose and bias it accordingly. ContinuingProperties can reference directly the UMultiAnimAsset
	// or any of the roled UMultiAnimAsset::GetAnimationAsset, and the UPoseSearchInteractionSubsystem will figure out the related UMultiAnimAsset
	// PoseHistoryName is the name of the pose history node used for the associated motion matching search
	// if bValidateResultAgainstAvailabilities is true, the result will be invalidated if doesn't respect the new availabilities
	UFUNCTION(Experimental, BlueprintPure, Category = "Animation|Pose Search", meta = (BlueprintThreadSafe))
	static FPoseSearchInteractionBlueprintResult MotionMatchInteraction_Pure(TArray<FPoseSearchInteractionAvailability> Availabilities, UObject* AnimInstance, FPoseSearchContinuingProperties ContinuingProperties, FName PoseHistoryName, bool bValidateResultAgainstAvailabilities);

	// BlueprintCallable version of MotionMatchInteraction_Pure
	UFUNCTION(Experimental, BlueprintCallable, Category = "Animation|Pose Search", meta = (BlueprintThreadSafe))
	static FPoseSearchInteractionBlueprintResult MotionMatchInteraction(TArray<FPoseSearchInteractionAvailability> Availabilities, UObject* AnimInstance, FPoseSearchContinuingProperties ContinuingProperties, FName PoseHistoryName, bool bValidateResultAgainstAvailabilities);

	// version of MotionMatchInteraction_Pure referencing directly the HistoryCollector rather than looking for it by name
	static FPoseSearchInteractionBlueprintResult MotionMatchInteraction(const TArrayView<const FPoseSearchInteractionAvailability> Availabilities, UObject* AnimInstance, const FPoseSearchContinuingProperties& ContinuingProperties, const FAnimNode_PoseSearchHistoryCollector_Base* HistoryCollector, bool bValidateResultAgainstAvailabilities);

	UFUNCTION(Experimental, BlueprintPure, Category = "Animation|Pose Search", meta = (BlueprintThreadSafe))
	static FPoseSearchContinuingProperties GetMontageContinuingProperties(UAnimInstance* AnimInstance);
};