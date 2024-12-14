// Copyright Epic Games, Inc. All Rights Reserved.


#include "MoverComponent.h"
#include "MoverSimulationTypes.h"
#include "MovementModeStateMachine.h"
#include "MotionWarpingMoverAdapter.h"
#include "DefaultMovementSet/Modes/WalkingMode.h"
#include "DefaultMovementSet/Modes/FallingMode.h"
#include "DefaultMovementSet/Modes/FlyingMode.h"
#include "MoveLibrary/MovementMixer.h"
#include "MoveLibrary/MovementUtils.h"
#include "MoveLibrary/FloorQueryUtils.h"
#include "MoverLog.h"
#include "InstantMovementEffect.h"
#include "Backends/MoverNetworkPredictionLiaison.h"
#include "Components/MeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/ScopedMovementUpdate.h"
#include "Engine/World.h"
#include "GameFramework/PhysicsVolume.h"
#include "Misc/AssertionMacros.h"
#include "Misc/TransactionObjectEvent.h"
#include "Blueprint/BlueprintExceptionInfo.h"
#include "UObject/ObjectSaveContext.h"
#include "Components/SkeletalMeshComponent.h"
#include "MotionWarpingComponent.h"


#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(MoverComponent)

#define LOCTEXT_NAMESPACE "Mover"


namespace MoverComponentConstants
{
	const FVector DefaultGravityAccel	= FVector(0.0, 0.0, -980.0);
	const FVector DefaultUpDir			= FVector(0.0, 0.0, 1.0);
}


static constexpr float ROTATOR_TOLERANCE = (1e-3);


UMoverComponent::UMoverComponent()
{
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	PrimaryComponentTick.bCanEverTick = true;

	BasedMovementTickFunction.bCanEverTick = true;
	BasedMovementTickFunction.bStartWithTickEnabled = false;
	BasedMovementTickFunction.SetTickFunctionEnable(false);
	BasedMovementTickFunction.TickGroup = TG_PostPhysics;

	bWantsInitializeComponent = true;
	bAutoActivate = true;

	PersistentSyncStateDataTypes.Add(FMoverDataPersistence(FMoverDefaultSyncState::StaticStruct(), true));

	BackendClass = UMoverNetworkPredictionLiaisonComponent::StaticClass();
}


void UMoverComponent::InitializeComponent()
{
	TGuardValue<bool> InInitializeComponentGuard(bInInitializeComponent, true);

	const UWorld* MyWorld = GetWorld();

	if (MyWorld && MyWorld->IsGameWorld())
	{
		FindDefaultUpdatedComponent();

		// Instantiate out sister backend component that will actually talk to the system driving the simulation
		if (BackendClass)
		{
			UActorComponent* NewLiaisonComp = NewObject<UActorComponent>(GetOwner(), BackendClass, TEXT("BackendLiaisonComponent"));
			BackendLiaisonComp = CastChecked<IMoverBackendLiaisonInterface>(NewLiaisonComp);
			if (BackendLiaisonComp.Get())
			{
				NewLiaisonComp->RegisterComponent();
				NewLiaisonComp->InitializeComponent();
				NewLiaisonComp->SetNetAddressable();
			}
		}
		else
		{
			UE_LOG(LogMover, Error, TEXT("No backend class set on %s. Mover actor will not function."), *GetNameSafe(GetOwner()));
		}
	}

	Super::InitializeComponent();
}


void UMoverComponent::UninitializeComponent()
{
	if (UActorComponent* LiaisonAsComp = Cast<UActorComponent>(BackendLiaisonComp.Get()))
	{
		LiaisonAsComp->DestroyComponent();
		BackendLiaisonComp = nullptr;
	}

	Super::UninitializeComponent();
}


void UMoverComponent::OnRegister()
{
	TGuardValue<bool> InOnRegisterGuard(bInOnRegister, true);

	Super::OnRegister();

	FindDefaultUpdatedComponent();
}


void UMoverComponent::RegisterComponentTickFunctions(bool bRegister)
{
	Super::RegisterComponentTickFunctions(bRegister);

	// Super may start up the tick function when we don't want to.
	UpdateTickRegistration();

	// If the owner ticks, make sure we tick first. This is to ensure the owner's location will be up to date when it ticks.
	AActor* Owner = GetOwner();

	if (bRegister && PrimaryComponentTick.bCanEverTick && Owner && Owner->CanEverTick())
	{
		Owner->PrimaryActorTick.AddPrerequisite(this, PrimaryComponentTick);
	}


	if (bRegister)
	{
		if (SetupActorComponentTickFunction(&BasedMovementTickFunction))
		{
			BasedMovementTickFunction.TargetMoverComp = this;
			BasedMovementTickFunction.AddPrerequisite(this, this->PrimaryComponentTick);
		}
	}
	else
	{
		if (BasedMovementTickFunction.IsTickFunctionRegistered())
		{
			BasedMovementTickFunction.UnRegisterTickFunction();
		}
	}
}


void UMoverComponent::PostLoad()
{
	Super::PostLoad();

	RefreshSharedSettings();
}

void UMoverComponent::BeginPlay()
{
	Super::BeginPlay();
	
	FindDefaultUpdatedComponent();
	ensureMsgf(UpdatedComponent != nullptr, TEXT("No root component found on %s. Simulation initialization will most likely fail."), *GetPathNameSafe(GetOwner()));

	if (const AActor* MyActor = GetOwner())
	{
		// If no primary visual component is already set, fall back to searching for any kind of mesh,
		// favoring a direct scene child of the UpdatedComponent.
		if (!PrimaryVisualComponent)
		{
			if (UpdatedComponent)
			{
				for (USceneComponent* ChildComp : UpdatedComponent->GetAttachChildren())
				{
					if (ChildComp->IsA<UMeshComponent>())
					{
						SetPrimaryVisualComponent(ChildComp);
						break;
					}
				}
			}

			if (!PrimaryVisualComponent)
			{
				SetPrimaryVisualComponent(MyActor->FindComponentByClass<UMeshComponent>());
			}
		}

		ensureMsgf(UpdatedComponent && (PrimaryVisualComponent != UpdatedComponent), TEXT("A Mover actor (%s) must have an UpdatedComponent and cannot have a PrimaryVisualComponent that is the same as UpdatedComponent"), *GetNameSafe(MyActor));

		// Optional motion warping support
		if (UMotionWarpingComponent* WarpingComp = MyActor->FindComponentByClass<UMotionWarpingComponent>())
		{
			UMotionWarpingMoverAdapter* WarpingAdapter = WarpingComp->CreateOwnerAdapter<UMotionWarpingMoverAdapter>();
			WarpingAdapter->SetMoverComp(this);
		}
	}

	// If an InputProducer isn't already set, check the actor and its components for one
	if (!InputProducer)
	{
		if (AActor* ActorOwner = GetOwner())
		{
			if (ActorOwner->GetClass()->ImplementsInterface(UMoverInputProducerInterface::StaticClass()))
			{
				InputProducer = ActorOwner;
			}
			else if (UActorComponent* FoundInputProducerComp = ActorOwner->FindComponentByInterface(UMoverInputProducerInterface::StaticClass()))
			{
				InputProducer = FoundInputProducerComp;
			}
		}
	}
	
	if (!MovementMixer)
	{
		MovementMixer = NewObject<UMovementMixer>(this, TEXT("Default Movement Mixer"));
	}
}

void UMoverComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

void UMoverComponent::BindProcessGeneratedMovement(FMover_ProcessGeneratedMovement ProcessGeneratedMovementEvent)
{
	ProcessGeneratedMovement = ProcessGeneratedMovementEvent;
}

void UMoverComponent::UnbindProcessGeneratedMovement()
{
	ProcessGeneratedMovement.Clear();
}

void UMoverComponent::ProduceInput(const int32 DeltaTimeMS, FMoverInputCmdContext* Cmd)
{
	Cmd->InputCollection.Empty();
	
	// Pass input production on to the right party
	if (InputProducer)
	{
		IMoverInputProducerInterface::Execute_ProduceInput(InputProducer, DeltaTimeMS, OUT *Cmd);
	}

	CachedLastProducedInputCmd = *Cmd;
	bHasValidLastProducedInput = true;

}

void UMoverComponent::RestoreFrame(const FMoverSyncState* SyncState, const FMoverAuxStateContext* AuxState)
{
	const FMoverSyncState& InvalidSyncState = GetSyncState();
	const FMoverAuxStateContext& InvalidAuxState = CachedLastAuxState;
	OnSimulationPreRollback(&InvalidSyncState, SyncState, &InvalidAuxState, AuxState);
	SetFrameStateFromContext(SyncState, AuxState, /* rebase? */ true);
	OnSimulationRollback(SyncState, AuxState);
}

void UMoverComponent::FinalizeFrame(const FMoverSyncState* SyncState, const FMoverAuxStateContext* AuxState)
{
	const FMoverDefaultSyncState* MoverState = SyncState->SyncStateCollection.FindDataByType<FMoverDefaultSyncState>();

	// TODO: Revisit this location check -- it seems simplistic now that we have composable state. Consider supporting a version that allows each sync state data struct a chance to react.
	// The component will often be in the "right place" already on FinalizeFrame, so a comparison check makes sense before setting it.
	if (MoverState &&
			(UpdatedComponent->GetComponentLocation().Equals(MoverState->GetLocation_WorldSpace()) == false ||
			 UpdatedComponent->GetComponentQuat().Rotator().Equals(MoverState->GetOrientation_WorldSpace(), ROTATOR_TOLERANCE) == false))
	{
		SetFrameStateFromContext(SyncState, AuxState, /* rebase? */ false);
	}
	else
	{
		// TODO: This is distasteful duplication -- consider moving to a util function
		CachedLastSyncState = *SyncState;
		CachedLastAuxState = *AuxState;
		CachedLastSimTickTimeStep.BaseSimTimeMs = BackendLiaisonComp->GetCurrentSimTimeMs();
		CachedLastSimTickTimeStep.ServerFrame = BackendLiaisonComp->GetCurrentSimFrame();
		bHasValidCachedState = true;
	}
}

void UMoverComponent::FinalizeSmoothingFrame(const FMoverSyncState* SyncState, const FMoverAuxStateContext* AuxState)
{
	if (PrimaryVisualComponent)
	{
		if (SmoothingMode == EMoverSmoothingMode::VisualComponentOffset)
		{
			// Offset the visual component so it aligns with the smoothed state transform, while leaving the actual root component in place
			if (const FMoverDefaultSyncState* MoverState = SyncState->SyncStateCollection.FindDataByType<FMoverDefaultSyncState>())
			{
				FTransform ActorTransform = FTransform(MoverState->GetOrientation_WorldSpace(), MoverState->GetLocation_WorldSpace(), FVector::OneVector);
				PrimaryVisualComponent->SetWorldTransform(BaseVisualComponentTransform * ActorTransform);	// smoothed location with base offset applied
			}
		}
		else
		{
			if (!PrimaryVisualComponent->GetRelativeTransform().Equals(BaseVisualComponentTransform))
			{
				PrimaryVisualComponent->SetRelativeTransform(BaseVisualComponentTransform);
			}
		}
	}
}

void UMoverComponent::TickInterpolatedSimProxy(const FMoverTimeStep& TimeStep, const FMoverInputCmdContext& InputCmd, UMoverComponent* MoverComp, const FMoverSyncState& CachedSyncState, const FMoverSyncState& SyncState, const FMoverAuxStateContext& AuxState)
{
	TArray<TSharedPtr<FMovementModifierBase>> ModifiersToStart;
	TArray<TSharedPtr<FMovementModifierBase>> ModifiersToEnd;

	for (auto ModifierFromSyncStateIt = SyncState.MovementModifiers.GetActiveModifiersIterator(); ModifierFromSyncStateIt; ++ModifierFromSyncStateIt)
	{
		const TSharedPtr<FMovementModifierBase> ModifierFromSyncState = *ModifierFromSyncStateIt;
		
		bool bContainsModifier = false;
		for (auto ModifierFromCacheIt = CachedSyncState.MovementModifiers.GetActiveModifiersIterator(); ModifierFromCacheIt; ++ModifierFromCacheIt)
		{
			const TSharedPtr<FMovementModifierBase> ModifierFromCache = *ModifierFromCacheIt;
			
			if (ModifierFromSyncState->Matches(ModifierFromCache.Get()))
			{
				bContainsModifier = true;
				break;
			}
		}

		if (!bContainsModifier)
		{
			ModifiersToStart.Add(ModifierFromSyncState);
		}
	}

	for (auto ModifierFromCacheIt = CachedSyncState.MovementModifiers.GetActiveModifiersIterator(); ModifierFromCacheIt; ++ModifierFromCacheIt)
	{
		const TSharedPtr<FMovementModifierBase> ModifierFromCache = *ModifierFromCacheIt;
		
		bool bContainsModifier = false;
		for (auto ModifierFromSyncStateIt = SyncState.MovementModifiers.GetActiveModifiersIterator(); ModifierFromSyncStateIt; ++ModifierFromSyncStateIt)
		{
			const TSharedPtr<FMovementModifierBase> ModifierFromSyncState = *ModifierFromSyncStateIt;
			
			if (ModifierFromSyncState->Matches(ModifierFromCache.Get()))
			{
				bContainsModifier = true;
				break;
			}
		}

		if (!bContainsModifier)
		{
			ModifiersToEnd.Add(ModifierFromCache);
		}
	}

	for (TSharedPtr<FMovementModifierBase> Modifier : ModifiersToStart)
	{
		Modifier->GenerateHandle();
		Modifier->OnStart(MoverComp, TimeStep, SyncState, AuxState);
	}

	for (auto ModifierIt = SyncState.MovementModifiers.GetActiveModifiersIterator(); ModifierIt; ++ModifierIt)
	{
		if (ModifierIt->IsValid())
		{
			ModifierIt->Get()->OnPreMovement(this, TimeStep);
			ModifierIt->Get()->OnPostMovement(this, TimeStep, SyncState, AuxState);
		}
	}

	for (TSharedPtr<FMovementModifierBase> Modifier : ModifiersToEnd)
	{
		Modifier->OnEnd(MoverComp, TimeStep, SyncState, AuxState);
	}
}


void UMoverComponent::InitializeSimulationState(FMoverSyncState* OutSync, FMoverAuxStateContext* OutAux)
{
	npCheckSlow(UpdatedComponent);
	npCheckSlow(OutSync);
	npCheckSlow(OutAux);

	// Add all initial persistent sync state types
	for (const FMoverDataPersistence& PersistentSyncEntry : PersistentSyncStateDataTypes)
	{
		check(PersistentSyncEntry.RequiredType && PersistentSyncEntry.RequiredType->IsChildOf(FMoverDataStructBase::StaticStruct()));
		OutSync->SyncStateCollection.FindOrAddDataByType(PersistentSyncEntry.RequiredType);
	}

	if (FMoverDefaultSyncState* MoverState = OutSync->SyncStateCollection.FindMutableDataByType<FMoverDefaultSyncState>())
	{
		MoverState->SetTransforms_WorldSpace(UpdatedComponent->GetComponentLocation(),
		                                     UpdatedComponent->GetComponentRotation(),
		                                     FVector::ZeroVector);	// no initial velocity
	}

	OutSync->MovementMode = StartingMovementMode;

	*OutAux = FMoverAuxStateContext();

	CachedLastSyncState = *OutSync;
	CachedLastAuxState = *OutAux;
	bHasValidCachedState = true;
}

void UMoverComponent::SimulationTick(const FMoverTimeStep& InTimeStep, const FMoverTickStartData& SimInput, OUT FMoverTickEndData& SimOutput)
{
	const bool bIsResimulating = InTimeStep.BaseSimTimeMs <= CachedNewestSimTickTimeStep.BaseSimTimeMs;

	FMoverTimeStep MoverTimeStep(InTimeStep);
	MoverTimeStep.bIsResimulating = bIsResimulating;

	if (bHasRolledBack)
	{
		ProcessFirstSimTickAfterRollback(InTimeStep);
	}

	OnPreSimulationTick.Broadcast(MoverTimeStep, SimInput.InputCmd);

	if (!ModeFSM->IsValidLowLevel())
	{
		SimOutput.SyncState = SimInput.SyncState;
		SimOutput.AuxState = SimInput.AuxState;
		return;
	}

	if (const FMoverDefaultSyncState* StartingSyncState = SimInput.SyncState.SyncStateCollection.FindDataByType<FMoverDefaultSyncState>())
	{
		if (!(UpdatedComponent->GetComponentLocation().Equals(StartingSyncState->GetLocation_WorldSpace()) || StartingSyncState->GetMovementBase()))
		{
			UE_LOG(LogMover, Warning, TEXT("%s %s: Simulation start location (%s) disagrees with actual mover component location (%s). This indicates movement of the component out-of-band with the simulation, and if happens often will lead to poor quality motion."),
			*GetNameSafe(GetOwner()),
			*StaticEnum<ENetRole>()->GetValueAsString(GetOwnerRole()),
			*StartingSyncState->GetLocation_WorldSpace().ToCompactString(),
			*UpdatedComponent->GetComponentLocation().ToCompactString());
		}
	}

	// Sync state data should carry over between frames
	for (const FMoverDataPersistence& PersistentSyncEntry : PersistentSyncStateDataTypes)
	{
		bool bShouldAddDefaultData = true;

		if (PersistentSyncEntry.bCopyFromPriorFrame)
		{
			if (const FMoverDataStructBase* PriorFrameData = SimInput.SyncState.SyncStateCollection.FindDataByType(PersistentSyncEntry.RequiredType))
			{
				SimOutput.SyncState.SyncStateCollection.AddOrOverwriteData(TSharedPtr<FMoverDataStructBase>(PriorFrameData->Clone()));
				bShouldAddDefaultData = false;
			}
		}

		if (bShouldAddDefaultData)
		{
			SimOutput.SyncState.SyncStateCollection.FindOrAddDataByType(PersistentSyncEntry.RequiredType);
		}
	}

	SimOutput.AuxState = SimInput.AuxState;

	FCharacterDefaultInputs* Input = SimInput.InputCmd.InputCollection.FindMutableDataByType<FCharacterDefaultInputs>();


	if (Input && !Input->SuggestedMovementMode.IsNone())
	{
		ModeFSM->QueueNextMode(Input->SuggestedMovementMode);
	}

	// Tick the actual simulation. This is where the proposed moves are queried and executed, affecting change to the moving actor's gameplay state and captured in the output sim state
	{
		FScopedMovementUpdate ScopedMovementUpdate(UpdatedComponent, EScopedUpdate::DeferredUpdates);
		ModeFSM->OnSimulationTick(UpdatedComponent, UpdatedCompAsPrimitive, SimBlackboard.Get(), SimInput, MoverTimeStep, SimOutput);
	}

	if (FMoverDefaultSyncState* OutputSyncState = SimOutput.SyncState.SyncStateCollection.FindMutableDataByType<FMoverDefaultSyncState>())
	{
		const FName MovementModeAfterTick = ModeFSM->GetCurrentModeName();
		SimOutput.SyncState.MovementMode = MovementModeAfterTick;

		if (UpdatedComponent->GetComponentLocation().Equals(OutputSyncState->GetLocation_WorldSpace()) == false ||
			UpdatedComponent->GetComponentQuat().Rotator().Equals(OutputSyncState->GetOrientation_WorldSpace(), ROTATOR_TOLERANCE) == false)
		{
			UE_LOG(LogMover, Warning, TEXT("Detected pos/rot difference between Mover actor (%s) sync state and scene component after sim ticking. This indicates a movement mode may not be authoring the final state correctly."), *GetNameSafe(UpdatedComponent->GetOwner()));
		}
	}

	// Note that we don't pull the rotation out of the final update transform. Converting back from a quat will lead to a different FRotator than what we are storing
	// here in the simulation layer. This may not be the best choice for all movement simulations, but is ok for this one.
	// JAH TODO: re-evaluate the above comment about handling the rotation data

	if (!SimOutput.MoveRecord.GetTotalMoveDelta().IsZero())
	{
		UE_LOG(LogMover, VeryVerbose, TEXT("KinematicSimTick: %s (role %i) frame %d: %s"),
			*GetNameSafe(UpdatedComponent->GetOwner()), UpdatedComponent->GetOwnerRole(), MoverTimeStep.ServerFrame, *SimOutput.MoveRecord.ToString());
	}

	OnPostMovement.Broadcast(MoverTimeStep, SimOutput.SyncState, SimOutput.AuxState);

	CachedLastUsedInputCmd = SimInput.InputCmd;
	bHasValidCachedUsedInput = true;

	if (bSupportsKinematicBasedMovement)
	{ 
		UpdateBasedMovementScheduling(SimOutput);
	}

	OnPostSimulationTick.Broadcast(MoverTimeStep);

	CachedLastSimTickTimeStep = MoverTimeStep;

	if (MoverTimeStep.ServerFrame > CachedNewestSimTickTimeStep.ServerFrame || MoverTimeStep.BaseSimTimeMs > CachedNewestSimTickTimeStep.BaseSimTimeMs)
	{
		CachedNewestSimTickTimeStep = MoverTimeStep;
	}	
}

UBaseMovementMode* UMoverComponent::FindMovementMode(TSubclassOf<UBaseMovementMode> MovementMode) const
{
	return FindMode_Mutable(MovementMode);
}

void UMoverComponent::K2_FindMovementModifier(FMovementModifierHandle ModifierHandle, bool& bFoundModifier, int32& TargetAsRawBytes) const
{
	// This will never be called, the exec version below will be hit instead
	checkNoEntry();
}

DEFINE_FUNCTION(UMoverComponent::execK2_FindMovementModifier)
{
	P_GET_STRUCT(FMovementModifierHandle, ModifierHandle);
	P_GET_UBOOL_REF(bFoundModifier);

	Stack.MostRecentPropertyAddress = nullptr;
	Stack.MostRecentPropertyContainer = nullptr;
	Stack.StepCompiledIn<FStructProperty>(nullptr);
	
	void* ModifierPtr = Stack.MostRecentPropertyAddress;
	FStructProperty* StructProp = CastField<FStructProperty>(Stack.MostRecentProperty);

	P_FINISH;

	bFoundModifier = false;
	
	if (!ModifierPtr)
	{
		FBlueprintExceptionInfo ExceptionInfo(
			EBlueprintExceptionType::AbortExecution,
			LOCTEXT("MoverComponent_GetActiveLayeredMove_UnresolvedTarget", "Failed to resolve the OutLayeredMove for GetActiveLayeredMove")
		);
	
		FBlueprintCoreDelegates::ThrowScriptException(P_THIS, Stack, ExceptionInfo);
	}
	else if (!StructProp)
	{
		FBlueprintExceptionInfo ExceptionInfo(
			EBlueprintExceptionType::AbortExecution,
			LOCTEXT("MoverComponent_GetActiveLayeredMove_TargetNotStruct", "GetActiveLayeredMove: Target for OutLayeredMove is not a valid type. It must be a Struct and a child of FLayeredMoveBase.")
		);
	
		FBlueprintCoreDelegates::ThrowScriptException(P_THIS, Stack, ExceptionInfo);
	}
	else if (!StructProp->Struct || !StructProp->Struct->IsChildOf(FMovementModifierBase::StaticStruct()))
	{
		FBlueprintExceptionInfo ExceptionInfo(
			EBlueprintExceptionType::AbortExecution,
			LOCTEXT("MoverComponent_GetActiveLayeredMove_BadType", "GetActiveLayeredMove: Target for OutLayeredMove is not a valid type. Must be a child of FLayeredMoveBase.")
		);
	
		FBlueprintCoreDelegates::ThrowScriptException(P_THIS, Stack, ExceptionInfo);
	}
	else
	{
		P_NATIVE_BEGIN;
		
		if (const FMovementModifierBase* FoundActiveMove = P_THIS->FindMovementModifier(ModifierHandle))
		{
			StructProp->Struct->CopyScriptStruct(ModifierPtr, FoundActiveMove);
			bFoundModifier = true;
		}

		P_NATIVE_END;
	}
}

bool UMoverComponent::IsModifierActiveOrQueued(const FMovementModifierHandle& ModifierHandle) const
{
	return FindMovementModifier(ModifierHandle) ? true : false;
}

const FMovementModifierBase* UMoverComponent::FindMovementModifier(const FMovementModifierHandle& ModifierHandle) const
{
	if (bHasValidCachedState)
	{
		// Check active modifiers for modifier handle
		for (auto ActiveModifierFromSyncStateIt = CachedLastSyncState.MovementModifiers.GetActiveModifiersIterator(); ActiveModifierFromSyncStateIt; ++ActiveModifierFromSyncStateIt)
		{
			const TSharedPtr<FMovementModifierBase> ActiveModifierFromSyncState = *ActiveModifierFromSyncStateIt;

			if (ModifierHandle == ActiveModifierFromSyncState->GetHandle())
			{
				return ActiveModifierFromSyncState.Get();
			}
		}

		// Check queued modifiers for modifier handle
		for (auto QueuedModifierFromSyncStateIt = CachedLastSyncState.MovementModifiers.GetQueuedModifiersIterator(); QueuedModifierFromSyncStateIt; ++QueuedModifierFromSyncStateIt)
		{
			const TSharedPtr<FMovementModifierBase> QueuedModifierFromSyncState = *QueuedModifierFromSyncStateIt;

			if (ModifierHandle == QueuedModifierFromSyncState->GetHandle())
			{
				return QueuedModifierFromSyncState.Get();
			}
		}
	}
	
	return nullptr;
}

const FMovementModifierBase* UMoverComponent::FindMovementModifierByType(const UScriptStruct* DataStructType) const
{
	if (bHasValidCachedState)
	{
		// Check active modifiers for modifier handle
		for (auto ActiveModifierFromSyncStateIt = CachedLastSyncState.MovementModifiers.GetActiveModifiersIterator(); ActiveModifierFromSyncStateIt; ++ActiveModifierFromSyncStateIt)
		{
			const TSharedPtr<FMovementModifierBase> ActiveModifierFromSyncState = *ActiveModifierFromSyncStateIt;

			if (DataStructType == ActiveModifierFromSyncState->GetScriptStruct())
			{
				return ActiveModifierFromSyncState.Get();
			}
		}

		// Check queued modifiers for modifier handle
		for (auto QueuedModifierFromSyncStateIt = CachedLastSyncState.MovementModifiers.GetQueuedModifiersIterator(); QueuedModifierFromSyncStateIt; ++QueuedModifierFromSyncStateIt)
		{
			const TSharedPtr<FMovementModifierBase> QueuedModifierFromSyncState = *QueuedModifierFromSyncStateIt;

			if (DataStructType == QueuedModifierFromSyncState->GetScriptStruct())
			{
				return QueuedModifierFromSyncState.Get();
			}
		}
	}
	
	return nullptr;
}

bool UMoverComponent::HasGameplayTag(FGameplayTag TagToFind, bool bExactMatch) const
{
	if (bHasValidCachedState)
	{
		// Search Movement Modes
		if (const UBaseMovementMode* ActiveMovementMode = GetMovementMode())
		{
			if (ActiveMovementMode->HasGameplayTag(TagToFind, bExactMatch))
			{
				return true;
			}
		}
		
		// Search Movement Modifiers
		for (auto ModifierFromSyncStateIt = CachedLastSyncState.MovementModifiers.GetActiveModifiersIterator(); ModifierFromSyncStateIt; ++ModifierFromSyncStateIt)
		{
			const TSharedPtr<FMovementModifierBase> ModifierFromSyncState = *ModifierFromSyncStateIt;
			if (ModifierFromSyncState->HasGameplayTag(TagToFind, bExactMatch))
			{
				return true;
			}
		}
		
		// Search Layered Moves
		for (auto LayeredMoveFromSyncStateIt = CachedLastSyncState.LayeredMoves.GetActiveMovesIterator(); LayeredMoveFromSyncStateIt; ++LayeredMoveFromSyncStateIt)
		{
			const TSharedPtr<FLayeredMoveBase> LayeredMoveFromSyncState = *LayeredMoveFromSyncStateIt;
			if (LayeredMoveFromSyncState->HasGameplayTag(TagToFind, bExactMatch))
			{
				return true;
			}
		}
	}
	
	return false;
}

void UMoverComponent::SetFrameStateFromContext(const FMoverSyncState* SyncState, const FMoverAuxStateContext* AuxState, bool bRebaseBasedState)
{

	// TODO: This is distasteful duplication -- consider moving to a util function
	// Copy these as the last official state, so they can be queried by other systems outside of the NP simulation
	CachedLastSyncState = *SyncState;
	CachedLastAuxState = *AuxState;
	CachedLastSimTickTimeStep.BaseSimTimeMs = BackendLiaisonComp->GetCurrentSimTimeMs();
	CachedLastSimTickTimeStep.ServerFrame = BackendLiaisonComp->GetCurrentSimFrame();
	bHasValidCachedState = true;

	if (FMoverDefaultSyncState* MoverState = CachedLastSyncState.SyncStateCollection.FindMutableDataByType<FMoverDefaultSyncState>())
	{
		if (bRebaseBasedState && MoverState->GetMovementBase())
		{
			// Note that this is modifying our cached mover state from what we received from Network Prediction. We are resampling
			// the transform of the movement base, in case it has changed as well during the rollback.
			MoverState->UpdateCurrentMovementBase();
		}

		// The state's properties are usually worldspace already, but may need to be adjusted to match the current movement base
		const FVector WorldLocation = MoverState->GetLocation_WorldSpace();
		const FRotator WorldOrientation = MoverState->GetOrientation_WorldSpace();
		const FVector WorldVelocity = MoverState->GetVelocity_WorldSpace();

		// Apply the desired transform to the scene component
		FTransform Transform(WorldOrientation, WorldLocation, UpdatedComponent->GetComponentTransform().GetScale3D());
		UpdatedComponent->SetWorldTransform(Transform, false, nullptr, ETeleportType::TeleportPhysics);
		UpdatedComponent->ComponentVelocity = WorldVelocity;
	}
}


bool UMoverComponent::InitMoverSimulation()
{
	check(UpdatedComponent);

	SimBlackboard = NewObject<UMoverBlackboard>(this, TEXT("MoverBlackboard"), RF_Transient);

	// Set up FSM and initial movement states
	ModeFSM = NewObject<UMovementModeStateMachine>(this, TEXT("MoverStateMachine"), RF_Transient);
	ModeFSM->ClearAllMovementModes();

	bool bHasMatchingStartingState = false;

	for (const TPair<FName, TObjectPtr<UBaseMovementMode>>& Element : MovementModes)
	{
		if (Element.Value.Get() == nullptr)
		{
			UE_LOG(LogMover, Warning, TEXT("Invalid Movement Mode type '%s' detected on %s. Mover actor will not function correctly."),
				*Element.Key.ToString(), *GetNameSafe(GetOwner()));
			continue;
		}

		ModeFSM->RegisterMovementMode(Element.Key, Element.Value);

		bHasMatchingStartingState |= (StartingMovementMode == Element.Key);
	}

	if (!bHasMatchingStartingState)
	{
		UE_LOG(LogMover, Warning, TEXT("Invalid StartingMovementMode '%s' specified on %s. Mover actor will not function."),
			*StartingMovementMode.ToString(), *GetNameSafe(GetOwner()));
	}

	if (bHasMatchingStartingState && StartingMovementMode != NAME_None)
	{
		ModeFSM->SetDefaultMode(StartingMovementMode);
		ModeFSM->QueueNextMode(StartingMovementMode);
	}

	return bHasMatchingStartingState;
}

void UMoverComponent::HandleImpact(FMoverOnImpactParams& ImpactParams)
{
	if (ImpactParams.MovementModeName.IsNone())
	{
		ImpactParams.MovementModeName = ModeFSM->GetCurrentModeName();
	}
	
	OnHandleImpact(ImpactParams);
}

void UMoverComponent::OnHandleImpact(const FMoverOnImpactParams& ImpactParams)
{
	// TODO: Handle physics impacts here - ie when player runs into box, impart force onto box
}

void UMoverComponent::UpdateBasedMovementScheduling(const FMoverTickEndData& SimOutput)
{
	// If we have a dynamic movement base, enable later based movement tick
	UPrimitiveComponent* SyncStateDynamicBase = nullptr;
	if (const FMoverDefaultSyncState* OutputSyncState = SimOutput.SyncState.SyncStateCollection.FindDataByType<FMoverDefaultSyncState>())
	{
		if (UBasedMovementUtils::IsADynamicBase(OutputSyncState->GetMovementBase()))
		{
			SyncStateDynamicBase = OutputSyncState->GetMovementBase();
		}
	}

	// Remove any stale dependency
	if (MovementBaseDependency && (MovementBaseDependency != SyncStateDynamicBase))
	{
		UBasedMovementUtils::RemoveTickDependency(BasedMovementTickFunction, MovementBaseDependency);
		MovementBaseDependency = nullptr;
	}

	// Set up current dependencies
	if (SyncStateDynamicBase)
	{
		BasedMovementTickFunction.SetTickFunctionEnable(true);

		if (UBasedMovementUtils::IsBaseSimulatingPhysics(SyncStateDynamicBase))
		{
			BasedMovementTickFunction.TickGroup = TG_PostPhysics;
		}
		else
		{
			BasedMovementTickFunction.TickGroup = TG_PrePhysics;
		}

		if (MovementBaseDependency == nullptr)
		{
			UBasedMovementUtils::AddTickDependency(BasedMovementTickFunction, SyncStateDynamicBase);
			MovementBaseDependency = SyncStateDynamicBase;
		}
	}
	else
	{
		BasedMovementTickFunction.SetTickFunctionEnable(false);
		MovementBaseDependency = nullptr;

		SimBlackboard->Invalidate(CommonBlackboard::LastFoundDynamicMovementBase);
		SimBlackboard->Invalidate(CommonBlackboard::LastAppliedDynamicMovementBase);
	}
}

void UMoverComponent::SetUpdatedComponent(USceneComponent* NewUpdatedComponent)
{
	// Remove delegates from old component
	if (UpdatedComponent)
	{
		UpdatedComponent->SetShouldUpdatePhysicsVolume(false);
		UpdatedComponent->SetPhysicsVolume(NULL, true);
		UpdatedComponent->PhysicsVolumeChangedDelegate.RemoveDynamic(this, &UMoverComponent::PhysicsVolumeChanged);

		// remove from tick prerequisite
		UpdatedComponent->PrimaryComponentTick.RemovePrerequisite(this, PrimaryComponentTick);
	}

	if (UpdatedCompAsPrimitive)
	{
		UpdatedCompAsPrimitive->OnComponentBeginOverlap.RemoveDynamic(this, &UMoverComponent::OnBeginOverlap);
	}

	// Don't assign pending kill components, but allow those to null out previous UpdatedComponent.
	UpdatedComponent = GetValid(NewUpdatedComponent);
	UpdatedCompAsPrimitive = Cast<UPrimitiveComponent>(UpdatedComponent);

	// Assign delegates
	if (IsValid(UpdatedComponent))
	{
		UpdatedComponent->SetShouldUpdatePhysicsVolume(true);
		UpdatedComponent->PhysicsVolumeChangedDelegate.AddUniqueDynamic(this, &UMoverComponent::PhysicsVolumeChanged);

		if (!bInOnRegister && !bInInitializeComponent)
		{
			// UpdateOverlaps() in component registration will take care of this.
			UpdatedComponent->UpdatePhysicsVolume(true);
		}

		// force ticks after movement component updates
		UpdatedComponent->PrimaryComponentTick.AddPrerequisite(this, PrimaryComponentTick);
	}

	if (IsValid(UpdatedCompAsPrimitive))
	{
		UpdatedCompAsPrimitive->OnComponentBeginOverlap.AddDynamic(this, &UMoverComponent::OnBeginOverlap);
	}


	UpdateTickRegistration();
}

void UMoverComponent::FindDefaultUpdatedComponent()
{
	if (!IsValid(UpdatedComponent))
	{
		USceneComponent* NewUpdatedComponent = nullptr;

		const AActor* MyActor = GetOwner();
		const UWorld* MyWorld = GetWorld();

		if (MyActor && MyWorld && MyWorld->IsGameWorld())
		{
			NewUpdatedComponent = MyActor->GetRootComponent();
		}

		SetUpdatedComponent(NewUpdatedComponent);
	}
}


void UMoverComponent::UpdateTickRegistration()
{
	const bool bHasUpdatedComponent = (UpdatedComponent != NULL);
	SetComponentTickEnabled(bHasUpdatedComponent && bAutoActivate);
}

void UMoverComponent::OnSimulationPreRollback(const FMoverSyncState* InvalidSyncState, const FMoverSyncState* SyncState, const FMoverAuxStateContext* InvalidAuxState, const FMoverAuxStateContext* AuxState)
{
	ModeFSM->OnSimulationPreRollback(InvalidSyncState, SyncState, InvalidAuxState, AuxState);
}


void UMoverComponent::OnSimulationRollback(const FMoverSyncState* SyncState, const FMoverAuxStateContext* AuxState)
{
	SimBlackboard->Invalidate(EInvalidationReason::Rollback);
	ModeFSM->OnSimulationRollback(SyncState, AuxState);

	bHasRolledBack = true;
}


void UMoverComponent::ProcessFirstSimTickAfterRollback(const FMoverTimeStep& TimeStep)
{
	OnPostSimulationRollback.Broadcast(TimeStep, CachedLastSimTickTimeStep);
	bHasRolledBack = false;
}


#if WITH_EDITOR

void UMoverComponent::PreSave(FObjectPreSaveContext ObjectSaveContext)
{
	Super::PreSave(ObjectSaveContext);

	RefreshSharedSettings();
}

void UMoverComponent::PostCDOCompiled(const FPostCDOCompiledContext& Context)
{
	Super::PostCDOCompiled(Context);

	RefreshSharedSettings();
}


void UMoverComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if ((PropertyChangedEvent.Property) && (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMoverComponent, MovementModes)))
	{
		RefreshSharedSettings();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}


void UMoverComponent::PostTransacted(const FTransactionObjectEvent& TransactionEvent)
{
	Super::PostTransacted(TransactionEvent);

	if ((TransactionEvent.GetEventType() == ETransactionObjectEventType::Finalized || TransactionEvent.GetEventType() == ETransactionObjectEventType::UndoRedo) &&
		TransactionEvent.HasPropertyChanges() &&
		TransactionEvent.GetChangedProperties().Contains(GET_MEMBER_NAME_CHECKED(UMoverComponent, MovementModes)))
	{
		RefreshSharedSettings();		
	}
}


EDataValidationResult UMoverComponent::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (!ValidateSetup(Context))
	{
		Result = EDataValidationResult::Invalid;
	}

	return Result;
}


bool UMoverComponent::ValidateSetup(FDataValidationContext& Context) const
{
	bool bHasMatchingStartingMode = false;
	bool bDidFindAnyProblems = false;

	// Verify all movement modes
	for (const TPair<FName, TObjectPtr<UBaseMovementMode>>& Element : MovementModes)
	{
		if (StartingMovementMode == Element.Key)
		{
			bHasMatchingStartingMode = true;
		}

		// Verify movement mode is valid
		if (!Element.Value)
		{
			Context.AddError(FText::Format(LOCTEXT("InvalidMovementModeError", "Invalid movement mode on {0}, mapped as {1}. Mover actor will not function."),
				FText::FromString(GetNameSafe(GetOwner())),
				FText::FromName(Element.Key)));

			bDidFindAnyProblems = true;
		}
		else if (Element.Value->IsDataValid(Context) == EDataValidationResult::Invalid)
		{
			bDidFindAnyProblems = true;
		}

		// Verify that the movement mode's shared settings object exists (if any)
		if (Element.Value)
		{
			for (TSubclassOf<UObject>& Type : Element.Value->SharedSettingsClasses)
			{
				if (Type.Get() == nullptr)
				{
					Context.AddError(FText::Format(LOCTEXT("InvalidModeSettingsError", "Movement mode on {0}, mapped as {1}, has an invalid SharedSettingsClass. You may need to remove the invalid settings class."),
						FText::FromString(GetNameSafe(GetOwner())),
						FText::FromName(Element.Key)));

					bDidFindAnyProblems = true;
				}
				else if (FindSharedSettings(Type) == nullptr)
				{
					Context.AddError(FText::Format(LOCTEXT("MissingModeSettingsError", "Movement mode on {0}, mapped as {1}, is missing its desired SharedSettingsClass {2}. You may need to save the asset and/or recompile."),
						FText::FromString(GetNameSafe(GetOwner())),
						FText::FromName(Element.Key),
						FText::FromString(Type->GetName())));

					bDidFindAnyProblems = true;
				}
			}
		}
	}

	// Verify we have a matching starting mode
	if (!bHasMatchingStartingMode && StartingMovementMode != NAME_None)
	{
		Context.AddError(FText::Format(LOCTEXT("InvalidStartingModeError", "Invalid StartingMovementMode {0} specified on {1}. Mover actor will not function."),
			FText::FromName(StartingMovementMode),
			FText::FromString(GetNameSafe(GetOwner()))));

		bDidFindAnyProblems = true;
	}

	// Verify transitions
	for (const UBaseMovementModeTransition* Transition : Transitions)
	{
		if (!IsValid(Transition))
		{
			Context.AddError(FText::Format(LOCTEXT("InvalidTransitionError", "Invalid or missing transition object on {0}. Clean up the Transitions array."),
				FText::FromString(GetNameSafe(GetOwner()))));

			bDidFindAnyProblems = true;
		}
	}

	// Verify backend liaison
	if (!BackendClass)
	{
		Context.AddError(FText::Format(LOCTEXT("MissingBackendClassError", "No BackendClass property specified on {0}. Mover actor will not function."),
			FText::FromString(GetNameSafe(GetOwner()))));

		bDidFindAnyProblems = true;
	}
	else if (!BackendClass->ImplementsInterface(UMoverBackendLiaisonInterface::StaticClass()))
	{
		Context.AddError(FText::Format(LOCTEXT("InvalidBackendClassError", "BackendClass {0} on {1} does not implement IMoverBackendLiaisonInterface. Mover actor will not function."),
			FText::FromString(BackendClass->GetName()),
			FText::FromString(GetNameSafe(GetOwner()))));

		bDidFindAnyProblems = true;
	}
	else
	{
		IMoverBackendLiaisonInterface* BackendCDOAsInterface = Cast<IMoverBackendLiaisonInterface>(BackendClass->GetDefaultObject());
		if (BackendCDOAsInterface && (BackendCDOAsInterface->ValidateData(Context, *this) == EDataValidationResult::Invalid))
		{
			bDidFindAnyProblems = true;
		}
	}

	// Verify persistent types
	for (const FMoverDataPersistence& PersistentSyncEntry : PersistentSyncStateDataTypes)
	{
		if (!PersistentSyncEntry.RequiredType || !PersistentSyncEntry.RequiredType->IsChildOf(FMoverDataStructBase::StaticStruct()))
		{
			Context.AddError(FText::Format(LOCTEXT("InvalidSyncStateTypeError", "RequiredType '{0}' is not a valid type or is missing. Must be a child of FMoverDataStructBase."),
				FText::FromString(GetNameSafe(PersistentSyncEntry.RequiredType))));

			bDidFindAnyProblems = true;
		}
	}

	
	return !bDidFindAnyProblems;
}

TArray<FString> UMoverComponent::GetStartingMovementModeNames()
{
	TArray<FString> PossibleModeNames;

	PossibleModeNames.Add(TEXT(""));

	for (const TPair<FName, TObjectPtr<UBaseMovementMode>>& Element : MovementModes)
	{
		FString ModeNameAsString;
		Element.Key.ToString(ModeNameAsString);
		PossibleModeNames.Add(ModeNameAsString);
	}

	return PossibleModeNames;
}

#endif // WITH_EDITOR


void UMoverComponent::PhysicsVolumeChanged(APhysicsVolume* NewVolume)
{
	// This itself feels bad. When will this be called? Its impossible to know what is allowed and not allowed to be done in this callback.
	// Callbacks instead should be trapped within the simulation update function. This isn't really possible though since the UpdateComponent
	// is the one that will call this.
}


void UMoverComponent::RefreshSharedSettings()
{
	TArray<TObjectPtr<UObject>> UnreferencedSettingsObjs = SharedSettings;

	// Add any missing settings
	for (const TPair<FName, TObjectPtr<UBaseMovementMode>>& Element : MovementModes)
	{
		if (UBaseMovementMode* Mode = Element.Value.Get())
		{
			for (TSubclassOf<UObject>& SharedSettingsType : Mode->SharedSettingsClasses)
			{
				if (SharedSettingsType.Get() == nullptr)
				{
					UE_LOG(LogMover, Warning, TEXT("Invalid shared setting class detected on Movement Mode %s."), *Mode->GetName());
					continue;
				}

				bool bFoundMatchingClass = false;
				for (const TObjectPtr<UObject>& SettingsObj : SharedSettings)
				{
					if (SettingsObj && SettingsObj->IsA(SharedSettingsType))
					{
						bFoundMatchingClass = true;
						UnreferencedSettingsObjs.Remove(SettingsObj);
						break;
					}
				}

				if (!bFoundMatchingClass)
				{
					UObject* NewSettings = NewObject<UObject>(this, SharedSettingsType, NAME_None, GetMaskedFlags(RF_PropagateToSubObjects) | RF_Transactional);
					SharedSettings.Add(NewSettings);
				}
			}
		}
	}

	// Remove any settings that are no longer used
	for (const TObjectPtr<UObject>& SettingsObjToRemove : UnreferencedSettingsObjs)
	{
		SharedSettings.Remove(SettingsObjToRemove);
	}

	// Sort by name for array order consistency
	Algo::Sort(SharedSettings, [](const TObjectPtr<UObject>& LHS, const TObjectPtr<UObject>& RHS) 
		{ return (LHS->GetClass()->GetPathName() < RHS.GetClass()->GetPathName()); });
}


void UMoverComponent::K2_QueueLayeredMove(const int32& MoveAsRawData)
{
	// This will never be called, the exec version below will be hit instead
	checkNoEntry();
}

DEFINE_FUNCTION(UMoverComponent::execK2_QueueLayeredMove)
{
	Stack.StepCompiledIn<FStructProperty>(nullptr);
	void* MovePtr = Stack.MostRecentPropertyAddress;
	FStructProperty* StructProp = CastField<FStructProperty>(Stack.MostRecentProperty);

	P_FINISH;

	P_NATIVE_BEGIN;

	const bool bHasValidStructProp = StructProp && StructProp->Struct && StructProp->Struct->IsChildOf(FLayeredMoveBase::StaticStruct());

	if (ensureMsgf((bHasValidStructProp && MovePtr), TEXT("An invalid type (%s) was sent to a QueueLayeredMove node. A struct derived from FLayeredMoveBase is required. No layered move will be queued."),
		StructProp ? *GetNameSafe(StructProp->Struct) : *Stack.MostRecentProperty->GetClass()->GetName()))
	{
		// Could we steal this instead of cloning? (move semantics)
		FLayeredMoveBase* MoveAsBasePtr = reinterpret_cast<FLayeredMoveBase*>(MovePtr);
		FLayeredMoveBase* ClonedMove = MoveAsBasePtr->Clone();

		P_THIS->QueueLayeredMove(TSharedPtr<FLayeredMoveBase>(ClonedMove));
	}

	P_NATIVE_END;
}


void UMoverComponent::QueueLayeredMove(TSharedPtr<FLayeredMoveBase> LayeredMove)
{	
	ModeFSM->QueueLayeredMove(LayeredMove);
}

FMovementModifierHandle UMoverComponent::K2_QueueMovementModifier(const int32& MoveAsRawData)
{
	// This will never be called, the exec version below will be hit instead
	checkNoEntry();
	return 0;
}

DEFINE_FUNCTION(UMoverComponent::execK2_QueueMovementModifier)
{
	Stack.StepCompiledIn<FStructProperty>(nullptr);
	void* MovePtr = Stack.MostRecentPropertyAddress;
	FStructProperty* StructProp = CastField<FStructProperty>(Stack.MostRecentProperty);

	P_FINISH;

	P_NATIVE_BEGIN;

	const bool bHasValidStructProp = StructProp && StructProp->Struct && StructProp->Struct->IsChildOf(FMovementModifierBase::StaticStruct());

	if (ensureMsgf((bHasValidStructProp && MovePtr), TEXT("An invalid type (%s) was sent to a QueueMovementModifier node. A struct derived from FMovementModifierBase is required. No modifier will be queued."),
		StructProp ? *GetNameSafe(StructProp->Struct) : *Stack.MostRecentProperty->GetClass()->GetName()))
	{
		// Could we steal this instead of cloning? (move semantics)
		FMovementModifierBase* MoveAsBasePtr = reinterpret_cast<FMovementModifierBase*>(MovePtr);
		FMovementModifierBase* ClonedMove = MoveAsBasePtr->Clone();

		FMovementModifierHandle ModifierID = P_THIS->QueueMovementModifier(TSharedPtr<FMovementModifierBase>(ClonedMove));
		*static_cast<FMovementModifierHandle*>(RESULT_PARAM) = ModifierID;
	}

	P_NATIVE_END;
}

FMovementModifierHandle UMoverComponent::QueueMovementModifier(TSharedPtr<FMovementModifierBase> Modifier)
{
	return ModeFSM->QueueMovementModifier(Modifier);
}

void UMoverComponent::CancelModifierFromHandle(FMovementModifierHandle ModifierHandle)
{
	ModeFSM->CancelModifierFromHandle(ModifierHandle);
}

void UMoverComponent::K2_QueueInstantMovementEffect(const int32& EffectAsRawData)
{
	// This will never be called, the exec version below will be hit instead
	checkNoEntry();
}

DEFINE_FUNCTION(UMoverComponent::execK2_QueueInstantMovementEffect)
{
	Stack.StepCompiledIn<FStructProperty>(nullptr);
	void* EffectPtr = Stack.MostRecentPropertyAddress;
	FStructProperty* StructProp = CastField<FStructProperty>(Stack.MostRecentProperty);

	P_FINISH;

	P_NATIVE_BEGIN;

	const bool bHasValidStructProp = StructProp && StructProp->Struct && StructProp->Struct->IsChildOf(FInstantMovementEffect::StaticStruct());

	if (ensureMsgf((bHasValidStructProp && EffectPtr), TEXT("An invalid type (%s) was sent to a QueueInstantMovementEffect node. A struct derived from FInstantMovementEffect is required. No Movement Effect will be queued."),
		StructProp ? *GetNameSafe(StructProp->Struct) : *Stack.MostRecentProperty->GetClass()->GetName()))
	{
		// Could we steal this instead of cloning? (move semantics)
		FInstantMovementEffect* EffectAsBasePtr = reinterpret_cast<FInstantMovementEffect*>(EffectPtr);
		FInstantMovementEffect* ClonedMove = EffectAsBasePtr->Clone();

		P_THIS->QueueInstantMovementEffect(TSharedPtr<FInstantMovementEffect>(ClonedMove));
	}

	P_NATIVE_END;
}


void UMoverComponent::QueueInstantMovementEffect(TSharedPtr<FInstantMovementEffect> InstantMovementEffect)
{	
	ModeFSM->QueueInstantMovementEffect(InstantMovementEffect);
}

void UMoverComponent::K2_FindActiveLayeredMove(bool& DidSucceed, int32& TargetAsRawBytes) const
{
	// This will never be called, the exec version below will be hit instead
	checkNoEntry();
}

DEFINE_FUNCTION(UMoverComponent::execK2_FindActiveLayeredMove)
{
	P_GET_UBOOL_REF(DidSucceed);

	Stack.MostRecentPropertyAddress = nullptr;
	Stack.MostRecentPropertyContainer = nullptr;
	Stack.StepCompiledIn<FStructProperty>(nullptr);
	
	void* MovePtr = Stack.MostRecentPropertyAddress;
	FStructProperty* StructProp = CastField<FStructProperty>(Stack.MostRecentProperty);

	P_FINISH;

	DidSucceed = false;
	
	if (!MovePtr)
	{
		FBlueprintExceptionInfo ExceptionInfo(
			EBlueprintExceptionType::AbortExecution,
			LOCTEXT("MoverComponent_GetActiveLayeredMove_UnresolvedTarget", "Failed to resolve the OutLayeredMove for GetActiveLayeredMove")
		);

		FBlueprintCoreDelegates::ThrowScriptException(P_THIS, Stack, ExceptionInfo);
	}
	else if (!StructProp)
	{
		FBlueprintExceptionInfo ExceptionInfo(
			EBlueprintExceptionType::AbortExecution,
			LOCTEXT("MoverComponent_GetActiveLayeredMove_TargetNotStruct", "GetActiveLayeredMove: Target for OutLayeredMove is not a valid type. It must be a Struct and a child of FLayeredMoveBase.")
		);

		FBlueprintCoreDelegates::ThrowScriptException(P_THIS, Stack, ExceptionInfo);
	}
	else if (!StructProp->Struct || !StructProp->Struct->IsChildOf(FLayeredMoveBase::StaticStruct()))
	{
		FBlueprintExceptionInfo ExceptionInfo(
			EBlueprintExceptionType::AbortExecution,
			LOCTEXT("MoverComponent_GetActiveLayeredMove_BadType", "GetActiveLayeredMove: Target for OutLayeredMove is not a valid type. Must be a child of FLayeredMoveBase.")
		);

		FBlueprintCoreDelegates::ThrowScriptException(P_THIS, Stack, ExceptionInfo);
	}
	else
	{
		P_NATIVE_BEGIN;
		
		if (const FLayeredMoveBase* FoundActiveMove = P_THIS->FindActiveLayeredMoveByType(StructProp->Struct))
		{
			StructProp->Struct->CopyScriptStruct(MovePtr, FoundActiveMove);
			DidSucceed = true;
		}

		P_NATIVE_END;
	}
}

const FLayeredMoveBase* UMoverComponent::FindActiveLayeredMoveByType(const UScriptStruct* DataStructType) const
{
	if (bHasValidCachedState)
	{
		for (auto it = CachedLastSyncState.LayeredMoves.GetActiveMovesIterator(); it; ++it)
		{
			UStruct* CandidateStruct = it->Get()->GetScriptStruct();
			while (CandidateStruct)
			{
				if (DataStructType == CandidateStruct)
				{
					return it->Get();
				}

				CandidateStruct = CandidateStruct->GetSuperStruct();
			}
		}
	}

	return nullptr;
}

void UMoverComponent::QueueNextMode(FName DesiredModeName, bool bShouldReenter)
{
	ModeFSM->QueueNextMode(DesiredModeName, bShouldReenter);
}

UBaseMovementMode* UMoverComponent::AddMovementModeFromClass(FName ModeName, TSubclassOf<UBaseMovementMode> MovementMode)
{
	if (!MovementMode)
	{
		UE_LOG(LogMover, Warning, TEXT("Attempted to add a movement mode that wasn't valid. AddMovementModeFromClass did not add anything."));
		return nullptr;
	}
	if (MovementMode->HasAnyClassFlags(CLASS_Abstract))
	{
		UE_LOG(LogMover, Warning, TEXT("The Movement Mode class (%s) is abstract and is not a valid class to instantiate. AddMovementModeFromClass will not do anything."), *GetNameSafe(MovementMode));
		return nullptr;
	}

	TObjectPtr<UBaseMovementMode> AddedMovementMode =  NewObject<UBaseMovementMode>(this, MovementMode);
	return AddMovementModeFromObject(ModeName, AddedMovementMode) ? AddedMovementMode : nullptr;
}

bool UMoverComponent::AddMovementModeFromObject(FName ModeName, UBaseMovementMode* MovementMode)
{
	if (MovementMode)
	{
		if (MovementMode->GetClass()->HasAnyClassFlags(CLASS_Abstract))
		{
			UE_LOG(LogMover, Warning, TEXT("The Movement Mode class (%s) is abstract and is not a valid class to instantiate. AddMovementModeFromObject will not do anything."), *GetNameSafe(MovementMode));
			return false;
		}
		
		if (TObjectPtr<UBaseMovementMode>* FoundMovementMode = MovementModes.Find(ModeName))
		{
			if (FoundMovementMode->Get()->GetClass() == MovementMode->GetClass())
			{
				UE_LOG(LogMover, Warning, TEXT("Added the same movement mode (%s) for a movement mode name (%s). AddMovementModeFromObject will add the mode but is likely unwanted/unnecessary behavior."), *GetNameSafe(MovementMode), *ModeName.ToString());
			}

			RemoveMovementMode(ModeName);
		}
		
		if (MovementMode->GetOuter() != this)
		{
			UE_LOG(LogMover, Verbose, TEXT("Movement modes are expected to be parented to the MoverComponent. The %s movement mode was reparented to %s!"), *GetNameSafe(MovementMode), *GetNameSafe(this));
			MovementMode->Rename(nullptr, this, REN_DoNotDirty | REN_NonTransactional);
		}
		
		MovementModes.Add(ModeName, MovementMode);
		ModeFSM->RegisterMovementMode(ModeName, MovementMode);
	}
	else
	{
		UE_LOG(LogMover, Warning, TEXT("Attempted to add %s movement mode that wasn't valid to %s. AddMovementModeFromObject did not add anything."), *GetNameSafe(MovementMode), *GetNameSafe(this));
		return false;
	}

	return true;
}

bool UMoverComponent::RemoveMovementMode(FName ModeName)
{
	if (ModeFSM->GetCurrentModeName() == ModeName)
	{
		UE_LOG(LogMover, Warning, TEXT("The mode being removed (%s Movement Mode) is the mode this actor is currently in. It was removed but may cause issues. Consider waiting to remove the mode or queueing a different valid mode to avoid issues."), *ModeName.ToString());
	}
	
	TObjectPtr<UBaseMovementMode>* ModeToRemove = MovementModes.Find(ModeName);
	const bool ModeRemoved = MovementModes.Remove(ModeName) > 0;
	if (ModeRemoved && ModeToRemove)
	{
		ModeFSM->UnregisterMovementMode(ModeName);
		ModeToRemove->Get()->ConditionalBeginDestroy();
	}
	
	return ModeRemoved; 
}


FTransform UMoverComponent::ConvertLocalRootMotionToWorld(const FTransform& LocalRootMotionTransform, float DeltaSeconds, const FTransform* AlternateActorToWorld, const FMotionWarpingUpdateContext* OptionalWarpingContext) const
{
	// Optionally process/warp localspace root motion
	const FTransform ProcessedLocalRootMotion = ProcessLocalRootMotionDelegate.IsBound()
		? ProcessLocalRootMotionDelegate.Execute(LocalRootMotionTransform, DeltaSeconds, OptionalWarpingContext)
		: LocalRootMotionTransform;

	// Convert processed localspace root motion to worldspace
	FTransform WorldSpaceRootMotion;

	if (USkeletalMeshComponent* SkeletalMesh = GetPrimaryVisualComponent<USkeletalMeshComponent>())
	{
		 WorldSpaceRootMotion = SkeletalMesh->ConvertLocalRootMotionToWorld(ProcessedLocalRootMotion);
	}
	else
	{
		const FTransform PresentationActorToWorldTransform = GetOwner()->GetTransform();
		const FVector DeltaWorldTranslation = ProcessedLocalRootMotion.GetTranslation() - PresentationActorToWorldTransform.GetTranslation();

		const FQuat NewWorldRotation = PresentationActorToWorldTransform.GetRotation() * ProcessedLocalRootMotion.GetRotation();
		const FQuat DeltaWorldRotation = NewWorldRotation * PresentationActorToWorldTransform.GetRotation().Inverse();

		WorldSpaceRootMotion.SetComponents(DeltaWorldRotation, DeltaWorldTranslation, FVector::OneVector);
	}

	// Optionally convert this to be relative to a different space
	if (AlternateActorToWorld)
	{
		const FTransform AlternateActorToWorldNoTrans(AlternateActorToWorld->GetRotation(), FVector::ZeroVector, AlternateActorToWorld->GetScale3D());

		FTransform WorldToActorNoTrans(GetOwner()->GetTransform().Inverse());
		WorldToActorNoTrans.SetTranslation(FVector::ZeroVector);

		const FTransform ActorSpaceRootMotion = WorldSpaceRootMotion * WorldToActorNoTrans;
		WorldSpaceRootMotion = ActorSpaceRootMotion * AlternateActorToWorldNoTrans;
	}
	
	// Optionally process/warp worldspace root motion
	return ProcessWorldRootMotionDelegate.IsBound()
		? ProcessWorldRootMotionDelegate.Execute(WorldSpaceRootMotion, DeltaSeconds, OptionalWarpingContext)
		: WorldSpaceRootMotion;
}


FTransform UMoverComponent::GetUpdatedComponentTransform() const
{
	if (UpdatedComponent)
	{
		return UpdatedComponent->GetComponentTransform();
	}
	return FTransform::Identity;
}


USceneComponent* UMoverComponent::GetUpdatedComponent() const
{
	return UpdatedComponent.Get();
}

USceneComponent* UMoverComponent::GetPrimaryVisualComponent() const
{
	return PrimaryVisualComponent.Get();
}

void UMoverComponent::SetPrimaryVisualComponent(USceneComponent* SceneComponent)
{
	if (SceneComponent && 
		ensureMsgf(SceneComponent->GetOwner() == GetOwner(), TEXT("Primary visual component must be owned by the same actor. MoverComp owner: %s  VisualComp owner: %s"), *GetNameSafe(GetOwner()), *GetNameSafe(SceneComponent->GetOwner())))
	{
		PrimaryVisualComponent = SceneComponent;
		BaseVisualComponentTransform = SceneComponent->GetRelativeTransform();
	}
	else
	{
		PrimaryVisualComponent = nullptr;
		BaseVisualComponentTransform = FTransform::Identity;
	}
}

FVector UMoverComponent::GetVelocity() const
{ 
	if (bHasValidCachedState)
	{
		if (const FMoverDefaultSyncState* SyncState = CachedLastSyncState.SyncStateCollection.FindDataByType<FMoverDefaultSyncState>())
		{
			return SyncState->GetVelocity_WorldSpace();
		}
	}

	return FVector::ZeroVector;
}


FVector UMoverComponent::GetMovementIntent() const
{ 
	if (bHasValidCachedState)
	{
		if (const FMoverDefaultSyncState* SyncState = CachedLastSyncState.SyncStateCollection.FindDataByType<FMoverDefaultSyncState>())
		{
			return SyncState->GetIntent_WorldSpace();
		}
	}

	return FVector::ZeroVector; 
}


FRotator UMoverComponent::GetTargetOrientation() const
{
	// Prefer the input's intended orientation, but if it can't be determined, assume it matches the actual orientation
	if (bHasValidCachedUsedInput)
	{
		const FMoverInputCmdContext& LastInputCmd = GetLastInputCmd();
		if (const FCharacterDefaultInputs* MoverInputs = LastInputCmd.InputCollection.FindDataByType<FCharacterDefaultInputs>())
		{
			const FVector TargetOrientationDir = MoverInputs->GetOrientationIntentDir_WorldSpace();

			if (!TargetOrientationDir.IsNearlyZero())
			{
				return TargetOrientationDir.ToOrientationRotator();
			}
		}
	}
	
	if (bHasValidCachedState)
	{
		if (const FMoverDefaultSyncState* SyncState = CachedLastSyncState.SyncStateCollection.FindDataByType<FMoverDefaultSyncState>())
		{
			return SyncState->GetOrientation_WorldSpace();
		}
	}

	return GetOwner() ? GetOwner()->GetActorRotation() : FRotator::ZeroRotator;
}


void UMoverComponent::SetGravityOverride(bool bOverrideGravity, FVector NewGravityAcceleration)
{
	bHasGravityOverride = bOverrideGravity;
	GravityAccelOverride = NewGravityAcceleration;
}


FVector UMoverComponent::GetGravityAcceleration() const
{
	if (bHasGravityOverride)
	{
		return GravityAccelOverride;
	}

	if (UpdatedComponent)
	{
		APhysicsVolume* CurPhysVolume = UpdatedComponent->GetPhysicsVolume();
		if (CurPhysVolume)
		{
			return CurPhysVolume->GetGravityZ() * FVector::UpVector;
		}
	}

	return MoverComponentConstants::DefaultGravityAccel;
}


FVector UMoverComponent::GetUpDirection() const
{
	const FVector DeducedUpDir = -GetGravityAcceleration().GetSafeNormal();

	if (DeducedUpDir.IsZero())
	{
		return MoverComponentConstants::DefaultUpDir;
	}

	return DeducedUpDir;
}

const FPlanarConstraint& UMoverComponent::GetPlanarConstraint() const
{
	return PlanarConstraint;
}

void UMoverComponent::SetPlanarConstraint(const FPlanarConstraint& InConstraint)
{
	PlanarConstraint = InConstraint;
}

TArray<FTrajectorySampleInfo> UMoverComponent::GetFutureTrajectory(float FutureSeconds, float SamplesPerSecond)
{
	FMoverPredictTrajectoryParams PredictionParams;
	PredictionParams.NumPredictionSamples = FMath::Max(1, FutureSeconds * SamplesPerSecond);
	PredictionParams.SecondsPerSample = FutureSeconds / (float)PredictionParams.NumPredictionSamples;

	return GetPredictedTrajectory(PredictionParams);
}

TArray<FTrajectorySampleInfo> UMoverComponent::GetPredictedTrajectory(FMoverPredictTrajectoryParams PredictionParams)
{
	if (ModeFSM)
	{
		FMoverTickStartData StartingState;

		StartingState.InputCmd = GetLastInputCmd();
		StartingState.SyncState = CachedLastSyncState;
		StartingState.AuxState = CachedLastAuxState;

		FMoverTickStartData StepState = StartingState;

		FMoverTimeStep FutureTimeStep;
		FutureTimeStep.StepMs = (PredictionParams.SecondsPerSample * 1000.f);
		FutureTimeStep.BaseSimTimeMs = CachedLastSimTickTimeStep.BaseSimTimeMs;
		FutureTimeStep.ServerFrame = 0;

		if (const UBaseMovementMode* CurrentMovementMode = ModeFSM->GetCurrentMode())
		{
			const bool bOrigHasGravityOverride = bHasGravityOverride;
			const FVector OrigGravityAccelOverride = GravityAccelOverride;

			if (PredictionParams.bDisableGravity)
			{
				SetGravityOverride(true, FVector::ZeroVector);
			}

			TArray<FTrajectorySampleInfo> OutSamples;
			OutSamples.AddDefaulted(PredictionParams.NumPredictionSamples);

			if (FMoverDefaultSyncState* StepSyncState = StepState.SyncState.SyncStateCollection.FindMutableDataByType<FMoverDefaultSyncState>())
			{
				FVector PriorLocation = StepSyncState->GetLocation_WorldSpace();
				FRotator PriorOrientation = StepSyncState->GetOrientation_WorldSpace();
				FVector PriorVelocity = StepSyncState->GetVelocity_WorldSpace();

				for (int32 i = 0; i < PredictionParams.NumPredictionSamples; ++i)
				{
					// Capture sample from current step state
					FTrajectorySampleInfo& Sample = OutSamples[i];

					Sample.Transform.SetLocation(StepSyncState->GetLocation_WorldSpace());
					Sample.Transform.SetRotation(StepSyncState->GetOrientation_WorldSpace().Quaternion());
					Sample.LinearVelocity = StepSyncState->GetVelocity_WorldSpace();
					Sample.InstantaneousAcceleration = (StepSyncState->GetVelocity_WorldSpace() - PriorVelocity) / PredictionParams.SecondsPerSample;
					Sample.AngularVelocity = (StepSyncState->GetOrientation_WorldSpace() - PriorOrientation) * (1.f / PredictionParams.SecondsPerSample);

					Sample.SimTimeMs = FutureTimeStep.BaseSimTimeMs;

					// Cache prior values
					PriorLocation = StepSyncState->GetLocation_WorldSpace();
					PriorOrientation = StepSyncState->GetOrientation_WorldSpace();
					PriorVelocity = StepSyncState->GetVelocity_WorldSpace();

					// Generate next move from current step state
					FProposedMove StepMove;
					CurrentMovementMode->DoGenerateMove(StepState, FutureTimeStep, StepMove);

					// Advance state based on move
					StepSyncState->SetTransforms_WorldSpace(StepSyncState->GetLocation_WorldSpace() + (StepMove.LinearVelocity * PredictionParams.SecondsPerSample),
						StepSyncState->GetOrientation_WorldSpace() + (StepMove.AngularVelocity * PredictionParams.SecondsPerSample),
						StepMove.LinearVelocity,
						StepSyncState->GetMovementBase(),
						StepSyncState->GetMovementBaseBoneName());

					FutureTimeStep.BaseSimTimeMs += FutureTimeStep.StepMs;
					++FutureTimeStep.ServerFrame;
				}

				// Put sample locations at visual root location if requested
				if (PredictionParams.bUseVisualComponentRoot)
				{
					if (const USceneComponent* VisualComp = GetPrimaryVisualComponent())
					{
						const FVector VisualCompOffset = VisualComp->GetRelativeLocation();
						const FTransform VisualCompRelativeTransform = VisualComp->GetRelativeTransform();

						for (int32 i=0; i < PredictionParams.NumPredictionSamples; ++i)
						{
							OutSamples[i].Transform = VisualCompRelativeTransform * OutSamples[i].Transform;
						}
					}
				}
			}

			if (PredictionParams.bDisableGravity)
			{
				SetGravityOverride(bOrigHasGravityOverride, OrigGravityAccelOverride);
			}

			return OutSamples;
		}
	}

	TArray<FTrajectorySampleInfo> BlankDefaultSamples;
	BlankDefaultSamples.AddDefaulted(PredictionParams.NumPredictionSamples);
	return BlankDefaultSamples;
}


FName UMoverComponent::GetMovementModeName() const
{ 
	if (bHasValidCachedState)
	{
		return CachedLastSyncState.MovementMode;
	}

	return NAME_None;
}

const UBaseMovementMode* UMoverComponent::GetMovementMode() const
{
	if (bHasValidCachedState)
	{
		if (const TObjectPtr<UBaseMovementMode>* CurrentMode = MovementModes.Find(CachedLastSyncState.MovementMode))
		{
			return CurrentMode->Get();
		}
	}

	return nullptr;
}

UPrimitiveComponent* UMoverComponent::GetMovementBase() const
{
	if (bHasValidCachedState)
	{
		if (const FMoverDefaultSyncState* SyncState = CachedLastSyncState.SyncStateCollection.FindDataByType<FMoverDefaultSyncState>())
		{
			return SyncState->GetMovementBase();
		}
	}

	return nullptr;
}

FName UMoverComponent::GetMovementBaseBoneName() const
{
	if (bHasValidCachedState)
	{
		if (const FMoverDefaultSyncState* SyncState = CachedLastSyncState.SyncStateCollection.FindDataByType<FMoverDefaultSyncState>())
		{
			return SyncState->GetMovementBaseBoneName();
		}
	}

	return NAME_None;
}


bool UMoverComponent::HasValidCachedState() const
{
	return bHasValidCachedState;
}

const FMoverSyncState& UMoverComponent::GetSyncState() const
{
	if (!bHasValidCachedState)
	{
		UE_LOG(LogMover, Warning, TEXT("Attempting direct access to the last-cached sync state before one has been set. Results will be unreliable. Use the HasValidCachedState function to check if CachedLastSyncState is valid or not."));
	}

	return CachedLastSyncState;
}

bool UMoverComponent::TryGetFloorCheckHitResult(FHitResult& OutHitResult) const
{
	FFloorCheckResult FloorCheck;
	if (SimBlackboard->TryGet(CommonBlackboard::LastFloorResult, FloorCheck))
	{
		OutHitResult = FloorCheck.HitResult;
		return true;
	}
	return false;
}

const UMoverBlackboard* UMoverComponent::GetSimBlackboard() const
{
	return SimBlackboard;
}

UMoverBlackboard* UMoverComponent::GetSimBlackboard_Mutable() const
{
	return SimBlackboard;
}

bool UMoverComponent::HasValidCachedInputCmd() const
{
	return bHasValidCachedUsedInput;
}

const FMoverInputCmdContext& UMoverComponent::GetLastInputCmd() const
{
	if (!bHasValidCachedUsedInput)
	{
		UE_LOG(LogMover, Warning, TEXT("Attempting direct access to the last-cached used input cmd before one has been set. Results will be unreliable. Use the HasValidCachedInputCmd function to check if CachedLastUsedInputCmd is valid or not."));
	}

	return CachedLastUsedInputCmd;
}

const FMoverTimeStep& UMoverComponent::GetLastTimeStep() const
{
	return CachedLastSimTickTimeStep;
}

IMovementSettingsInterface* UMoverComponent::FindSharedSettings_Mutable(const UClass* ByType) const
{
	check(ByType);

	for (const TObjectPtr<UObject>& SettingsObj : SharedSettings)
	{
		if (SettingsObj->IsA(ByType))
		{
			return Cast<IMovementSettingsInterface>(SettingsObj);
		}
	}

	return nullptr;
}

UObject* UMoverComponent::FindSharedSettings_Mutable_BP(TSubclassOf<UObject> SharedSetting) const
{
	if (SharedSetting->ImplementsInterface(UMovementSettingsInterface::StaticClass()))
    {
    	return Cast<UObject>(FindSharedSettings_Mutable(SharedSetting));
    }
    
    return nullptr;
}

const UObject* UMoverComponent::FindSharedSettings_BP(TSubclassOf<UObject> SharedSetting) const
{
	if (SharedSetting->ImplementsInterface(UMovementSettingsInterface::StaticClass()))
	{
		return Cast<UObject>(FindSharedSettings(SharedSetting));
	}

	return nullptr;
}

UBaseMovementMode* UMoverComponent::FindMode_Mutable(const UClass* ByType, bool bRequireExactClass) const
{
	for (const TPair<FName, TObjectPtr<UBaseMovementMode>>& Element : MovementModes)
	{
		if ( (!bRequireExactClass && Element.Value->IsA(ByType)) || 
		     (Element.Value->GetClass() == ByType) )
		{
			return Element.Value.Get();
		}
	}

	return nullptr;
}

#undef LOCTEXT_NAMESPACE
