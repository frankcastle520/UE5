// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ChaosVDDataContainerBaseActor.h"
#include "ChaosVDParticleActor.h"
#include "ChaosVDSceneSelectionObserver.h"
#include "GameFramework/Actor.h"
#include "ChaosVDSolverInfoActor.generated.h"

class UChaosVDGTAccelerationStructuresDataComponent;
class UChaosVDSolverCharacterGroundConstraintDataComponent;
class UChaosVDSolverJointConstraintDataComponent;
struct FChaosVDParticleDataWrapper;
class AChaosVDParticleActor;
class UChaosVDGenericDebugDrawDataComponent;
class UChaosVDParticleDataComponent;
class UChaosVDSceneQueryDataComponent;
class UChaosVDSolverCollisionDataComponent;
struct FChaosVDGameFrameData;

enum class EChaosVDParticleType : uint8;

/** Actor that contains all relevant data for the current visualized solver frame */
UCLASS(NotBlueprintable, NotPlaceable)
class AChaosVDSolverInfoActor : public AChaosVDDataContainerBaseActor, public FChaosVDSceneSelectionObserver
{
	GENERATED_BODY()

public:

	AChaosVDSolverInfoActor();

	void SetSolverID(int32 InSolverID);
	int32 GetSolverID() const { return SolverID; }

	void SetSolverName(const FName& InSolverName);
	const FName& GetSolverName() { return SolverName; }

	void SetIsServer(bool bInIsServer) { bIsServer = bInIsServer; }
	bool GetIsServer() const { return bIsServer; }

	virtual void SetScene(TWeakPtr<FChaosVDScene> InScene) override;

	void SetSimulationTransform(const FTransform& InSimulationTransform) { SimulationTransform = InSimulationTransform; }
	const FTransform& GetSimulationTransform() const { return SimulationTransform; }

	UChaosVDSolverCollisionDataComponent* GetCollisionDataComponent() { return CollisionDataComponent; }
	UChaosVDParticleDataComponent* GetParticleDataComponent() { return ParticleDataComponent; }
	UChaosVDSolverJointConstraintDataComponent* GetJointsDataComponent() { return JointsDataComponent; }
	UChaosVDSolverCharacterGroundConstraintDataComponent* GetCharacterGroundConstraintDataComponent() { return CharacterGroundConstraintDataComponent; }
	UChaosVDGTAccelerationStructuresDataComponent* GetGTAccelerationStructuresDataComponent() { return GTAccelerationStructuresDataComponent; }
	UChaosVDSceneQueryDataComponent* GetSceneQueryDataComponent() const { return SceneQueryDataComponent.Get(); }
	UChaosVDGenericDebugDrawDataComponent* GetGenericDebugDrawDataComponent() const { return GenericDebugDrawDataComponent.Get(); }

	void RegisterParticleActor(int32 ParticleID, AChaosVDParticleActor* ParticleActor);

	AChaosVDParticleActor* GetParticleActor(int32 ParticleID);
	const TMap<int32, TObjectPtr<AChaosVDParticleActor>>& GetAllParticleActorsByIDMap() { return  SolverParticlesByID; }

	const TArray<int32>& GetSelectedParticlesIDs() const { return SelectedParticlesID; }

	bool IsParticleSelectedByID(int32 ParticleID);

	bool SelectParticleByID(int32 ParticleIDToSelect);

	template <typename TCallback>
	void VisitSelectedParticleData(TCallback VisitCallback);

	template <typename TCallback>
	void VisitAllParticleData(TCallback VisitCallback);

	void RemoveSolverFolders(UWorld* World);

	virtual bool IsVisible() const override;

#if WITH_EDITOR
	void SetIsTemporarilyHiddenInEditor(bool bIsHidden) override;
#endif
	virtual void Destroyed() override;

protected:

	void HandleVisibilitySettingsUpdated(UObject* SettingsObject);
	void HandleColorsSettingsUpdated(UObject* SettingsObject);

	void ApplySolverVisibilityToParticle(AChaosVDParticleActor* ParticleActor, bool bIsHidden);

	virtual void HandlePostSelectionChange(const UTypedElementSelectionSet* ChangesSelectionSet) override;

	FName GetFolderPathForParticleType(EChaosVDParticleType ParticleType);

	UPROPERTY(VisibleAnywhere, Category="Solver Data")
	int32 SolverID = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, Category="Solver Data")
	FTransform SimulationTransform;

	UPROPERTY(VisibleAnywhere, Category="Solver Data")
	FName SolverName;

	UPROPERTY()
	TObjectPtr<UChaosVDSolverCollisionDataComponent> CollisionDataComponent;

	UPROPERTY()
	TMap<int32, TObjectPtr<AChaosVDParticleActor>> SolverParticlesByID;

	TArray<int32> SelectedParticlesID;

	TSortedMap<EChaosVDParticleType, FName> FolderPathByParticlePath;

	TSet<FFolder> CreatedFolders;

	bool bIsServer;

	UPROPERTY()
	TObjectPtr<UChaosVDParticleDataComponent> ParticleDataComponent;

	UPROPERTY()
	TObjectPtr<UChaosVDSolverJointConstraintDataComponent> JointsDataComponent;

	UPROPERTY()
	TObjectPtr<UChaosVDSolverCharacterGroundConstraintDataComponent> CharacterGroundConstraintDataComponent;

	UPROPERTY()
	TObjectPtr<UChaosVDGTAccelerationStructuresDataComponent> GTAccelerationStructuresDataComponent;

	UPROPERTY()
	TObjectPtr<UChaosVDSceneQueryDataComponent> SceneQueryDataComponent;
	
	UPROPERTY()
	TObjectPtr<UChaosVDGenericDebugDrawDataComponent> GenericDebugDrawDataComponent;
};

template <typename TCallback>
void AChaosVDSolverInfoActor::VisitSelectedParticleData(TCallback VisitCallback)
{
	for (const int32 SelectedParticleID : SelectedParticlesID)
	{
		AChaosVDParticleActor* ParticleActor = GetParticleActor(SelectedParticleID);
		TSharedPtr<const FChaosVDParticleDataWrapper> ParticleDataViewer = ParticleActor ? ParticleActor->GetParticleData() : nullptr;
		if (!ensure(ParticleDataViewer))
		{
			continue;
		}

		if (!VisitCallback(ParticleDataViewer))
		{
			return;
		}
	}
}

template <typename TCallback>
void AChaosVDSolverInfoActor::VisitAllParticleData(TCallback VisitCallback)
{
	for (const TPair<int32, TObjectPtr<AChaosVDParticleActor>>& ParticleWithIDPair : SolverParticlesByID)
	{
		AChaosVDParticleActor* ParticleActor = ParticleWithIDPair.Value;
		TSharedPtr<const FChaosVDParticleDataWrapper> ParticleDataViewer = ParticleActor ? ParticleActor->GetParticleData() : nullptr;
		if (!ensure(ParticleDataViewer))
		{
			continue;
		}

		if (!VisitCallback(ParticleDataViewer))
		{
			return;
		}
	}
}
