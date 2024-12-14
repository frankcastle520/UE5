// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "MovementMode.h"
#include "InstantMovementEffect.h"
#include "MovementModeStateMachine.generated.h"

struct FProposedMove;
class UImmediateMovementModeTransition;

/**
 * NullMovementMode: a default do-nothing mode used as a placeholder when no other mode is active
 */
 UCLASS(NotBlueprintable)
class UNullMovementMode : public UBaseMovementMode
{
	GENERATED_UCLASS_BODY()

public:
	virtual void OnSimulationTick(const FSimulationTickParams& Params, FMoverTickEndData& OutputState) override;

	const static FName NullModeName;
};

/**
 * - Any movement modes registered are co-owned by the state machine
 * - There is always an active mode, falling back to a do-nothing 'null' mode
 * - Queuing a mode that is already active will cause it to exit and re-enter
 * - Modes only switch during simulation tick
 */
 UCLASS()
class UMovementModeStateMachine : public UObject
{
	 GENERATED_UCLASS_BODY()

public:
	void RegisterMovementMode(FName ModeName, TObjectPtr<UBaseMovementMode> Mode, bool bIsDefaultMode=false);
	void RegisterMovementMode(FName ModeName, TSubclassOf<UBaseMovementMode> ModeType, bool bIsDefaultMode=false);

	void UnregisterMovementMode(FName ModeName);
	void UnregisterMovementMode(TObjectPtr<UBaseMovementMode> ModeToUnregister);
	void ClearAllMovementModes();

	void SetDefaultMode(FName NewDefaultModeName);

	void QueueNextMode(FName DesiredNextModeName, bool bShouldReenter=false);
	void SetModeImmediately(FName DesiredModeName, bool bShouldReenter=false);
	void ClearQueuedMode();

	void OnSimulationTick(USceneComponent* UpdatedComponent, UPrimitiveComponent* UpdatedPrimitive, UMoverBlackboard* SimBlackboard, const FMoverTickStartData& StartState, const FMoverTimeStep& TimeStep, FMoverTickEndData& OutputState);
 	void OnSimulationPreRollback(const FMoverSyncState* InvalidSyncState, const FMoverSyncState* SyncState, const FMoverAuxStateContext* InvalidAuxState, const FMoverAuxStateContext* AuxState);
	void OnSimulationRollback(const FMoverSyncState* SyncState, const FMoverAuxStateContext* AuxState);

	FName GetCurrentModeName() const { return CurrentModeName; }

	const UBaseMovementMode* GetCurrentMode() const;

	const UBaseMovementMode* FindMovementMode(FName ModeName) const;

	void QueueLayeredMove(TSharedPtr<FLayeredMoveBase> Move);
	
 	void QueueInstantMovementEffect(TSharedPtr<FInstantMovementEffect> Effect);

	FMovementModifierHandle QueueMovementModifier(TSharedPtr<FMovementModifierBase> Modifier);

 	void CancelModifierFromHandle(FMovementModifierHandle ModifierHandle);
 	
protected:

	virtual void PostInitProperties() override;

	UPROPERTY()
	TMap<FName, TObjectPtr<UBaseMovementMode>> Modes;

	UPROPERTY(Transient)
	TObjectPtr<UImmediateMovementModeTransition> QueuedModeTransition;

	FName DefaultModeName = NAME_None;
	FName CurrentModeName = NAME_None;

	/** Moves that are queued to be added to the simulation at the start of the next sim subtick */
	TArray<TSharedPtr<FLayeredMoveBase>> QueuedLayeredMoves;

 	/** Effects that are queued to be applied to the simulation at the start of the next sim subtick or at the end of this tick */
 	TArray<TSharedPtr<FInstantMovementEffect>> QueuedInstantEffects;

 	/** Modifiers that are queued to be added to the simulation at the start of the next sim subtick */
 	TArray<TSharedPtr<FMovementModifierBase>> QueuedMovementModifiers;

 	/** Modifiers that are to be canceled at the start of the next sim subtick */
 	TArray<FMovementModifierHandle> ModifiersToCancel;
 	
private:
	void ConstructDefaultModes();
	void AdvanceToNextMode();
	void FlushQueuedMovesToGroup(FLayeredMoveGroup& Group);
 	void FlushQueuedModifiersToGroup(FMovementModifierGroup& ModifierGroup);
 	void FlushModifierCancellationsToGroup(FMovementModifierGroup& ActiveModifierGroup);
 	void RollbackModifiers(const FMoverSyncState* InvalidSyncState, const FMoverSyncState* SyncState, const FMoverAuxStateContext* InvalidAuxState, const FMoverAuxStateContext* AuxState);
 	bool ApplyInstantEffects(FApplyMovementEffectParams& ApplyEffectParams, FMoverSyncState& OutputState);
	AActor* GetOwnerActor() const;
};



 /**
  * Simple transition that evaluates true if a "next mode" is set. Used internally only by the Mover plugin. 
  */
 UCLASS()
 class UImmediateMovementModeTransition : public UBaseMovementModeTransition
 {
	 GENERATED_UCLASS_BODY()

 public:
	 virtual FTransitionEvalResult OnEvaluate(const FSimulationTickParams& Params) const override;
	 virtual void OnTrigger(const FSimulationTickParams& Params) override;


	 bool IsSet() const { return !NextMode.IsNone(); }
	 void SetNextMode(FName DesiredModeName, bool bShouldReenter = false);
	 void Clear();



	 FName GetNextModeName() const { return NextMode; }
	 bool ShoulReenter() const { return bShouldNextModeReenter; }

 private:
	 FName NextMode = NAME_None;
	 bool bShouldNextModeReenter = false;
 };
