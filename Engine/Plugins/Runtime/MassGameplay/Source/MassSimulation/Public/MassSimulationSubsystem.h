// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Delegates/IDelegateInstance.h"
#include "MassProcessingTypes.h"
#include "MassProcessingPhaseManager.h"
#include "MassSubsystemBase.h"
#include "MassSimulationSubsystem.generated.h"


struct FMassEntityManager;
class IConsoleVariable;

DECLARE_LOG_CATEGORY_EXTERN(LogMassSim, Log, All);

UCLASS(config = Game, defaultconfig, MinimalAPI)
class UMassSimulationSubsystem : public UMassSubsystemBase
{
	GENERATED_BODY()
public:
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnSimulationStarted, UWorld* /*World*/);
	
	MASSSIMULATION_API UMassSimulationSubsystem(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	const FMassProcessingPhaseManager& GetPhaseManager() const { return PhaseManager; }

	MASSSIMULATION_API FMassProcessingPhase::FOnPhaseEvent& GetOnProcessingPhaseStarted(const EMassProcessingPhase Phase);
	MASSSIMULATION_API FMassProcessingPhase::FOnPhaseEvent& GetOnProcessingPhaseFinished(const EMassProcessingPhase Phase);
	static FOnSimulationStarted& GetOnSimulationStarted() { return OnSimulationStarted; }

	MASSSIMULATION_API void RegisterDynamicProcessor(UMassProcessor& Processor);
	MASSSIMULATION_API void UnregisterDynamicProcessor(UMassProcessor& Processor);

	bool IsSimulationStarted() const { return bSimulationStarted; }

	/** @return whether hosted EntityManager is currently, actively being used for processing purposes. Equivalent to calling FMassEntityManager.IsProcessing() */
	MASSSIMULATION_API bool IsDuringMassProcessing() const;

	/** Starts/stops simulation ticking for all worlds, based on new `mass.SimulationTickingEnabled` cvar value */
	MASSSIMULATION_API static void HandleSimulationTickingEnabledCVarChange(IConsoleVariable*);

protected:
	// UWorldSubsystem BEGIN
	MASSSIMULATION_API virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	MASSSIMULATION_API virtual void PostInitialize() override;
	MASSSIMULATION_API virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	MASSSIMULATION_API virtual void Deinitialize() override;
	// UWorldSubsystem END
	MASSSIMULATION_API virtual void BeginDestroy() override;
	
	MASSSIMULATION_API void RebuildTickPipeline();

	MASSSIMULATION_API void StartSimulation(UWorld& InWorld);
	MASSSIMULATION_API void StopSimulation();

	MASSSIMULATION_API void OnProcessingPhaseStarted(const float DeltaSeconds, const EMassProcessingPhase Phase) const;

#if WITH_EDITOR
	MASSSIMULATION_API void OnPieBegin(const bool bIsSimulation);
	MASSSIMULATION_API void OnPieEnded(const bool bIsSimulation);
	MASSSIMULATION_API void OnMassEntitySettingsChange(const FPropertyChangedEvent& PropertyChangedEvent);
#endif // WITH_EDITOR

protected:

	TSharedPtr<FMassEntityManager> EntityManager;

	FMassProcessingPhaseManager PhaseManager;

	inline static FOnSimulationStarted OnSimulationStarted={};

	UPROPERTY()
	FMassRuntimePipeline RuntimePipeline;

	float CurrentDeltaSeconds = 0.f;
	bool bTickInProgress = false;
	bool bSimulationStarted = false;

#if WITH_EDITOR
	FDelegateHandle PieBeginEventHandle;
	FDelegateHandle PieEndedEventHandle;

	FDelegateHandle MassEntitySettingsChangeHandle;
#endif // WITH_EDITOR
};
