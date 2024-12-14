// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GeometryCollection/GeometryCollectionComponent.h"
#include "ChaosModularVehicle/ChaosSimModuleManagerAsyncCallback.h"
#include "SimModule/SimModuleTree.h"
#include "SimModule/SimModulesInclude.h"
#include "ModularVehicleBuilder.h"

struct FModularVehicleAsyncInput;
struct FChaosSimModuleManagerAsyncOutput;
struct FModuleInputContainer;

struct CHAOSMODULARVEHICLEENGINE_API FModularVehicleDebugParams
{
	bool ShowDebug = false;
	bool SuspensionRaycastsEnabled = true;
	bool ShowSuspensionRaycasts = false;
	bool ShowWheelData = false;
	bool ShowRaycastMaterial = false;
	bool ShowWheelCollisionNormal = false;

	bool DisableAnim = false;
	float FrictionOverride = 1.0f;
};

namespace Chaos
{
	class FClusterUnionPhysicsProxy;
}


class CHAOSMODULARVEHICLEENGINE_API FModularVehicleSimulationCU
{
public:
	using FInputNameMap = FInputInterface::FInputNameMap;

	FModularVehicleSimulationCU(bool InUsingNetworkPhysicsPrediction, int8 InNetMode)
		: bUsingNetworkPhysicsPrediction(InUsingNetworkPhysicsPrediction)
		, NetMode(InNetMode)
	{
	}

	virtual ~FModularVehicleSimulationCU()
	{
		SimModuleTree.Reset();
	}

	void Initialize(TUniquePtr<Chaos::FSimModuleTree>& InSimModuleTree);
	void Terminate();
	void SetInputMappings(const FInputNameMap& InNameMap)
	{ 
		FWriteScopeLock InputConfigLock(InputConfigurationLock);
		InputNameMap = InNameMap; 
	}

	/** Update called from Physics Thread */
	virtual void Simulate(UWorld* InWorld, float DeltaSeconds, const FModularVehicleAsyncInput& InputData, FModularVehicleAsyncOutput& OutputData, IPhysicsProxyBase* Proxy);
	virtual void Simulate_ClusterUnion(UWorld* InWorld, float DeltaSeconds, const FModularVehicleAsyncInput& InputData, FModularVehicleAsyncOutput& OutputData, Chaos::FClusterUnionPhysicsProxy* Proxy);

	virtual void OnContactModification(Chaos::FCollisionContactModifier& Modifier, IPhysicsProxyBase* Proxy);

	void ApplyDeferredForces(FGeometryCollectionPhysicsProxy* RigidHandle);
	void ApplyDeferredForces(Chaos::FClusterUnionPhysicsProxy* Proxy);

	void PerformAdditionalSimWork(UWorld* InWorld, const FModularVehicleAsyncInput& InputData, Chaos::FClusterUnionPhysicsProxy* Proxy, Chaos::FAllInputs& AllInputs);

	void FillOutputState(FModularVehicleAsyncOutput& Output);

	const TUniquePtr<Chaos::FSimModuleTree>& GetSimComponentTree() const {
		Chaos::EnsureIsInPhysicsThreadContext();
		return SimModuleTree;
		}

	TUniquePtr<Chaos::FSimModuleTree>& AccessSimComponentTree() {
		return SimModuleTree; 
		}

	TUniquePtr<Chaos::FSimModuleTree> SimModuleTree;	/* Simulation modules stored in tree structure */
	Chaos::FAllInputs SimInputData;
	bool bUsingNetworkPhysicsPrediction;

	/** Current control inputs that is being used on the PT */
	FModularVehicleInputs VehicleInputs;
	FInputNameMap InputNameMap;
	FRWLock InputConfigurationLock;

	int8 NetMode;

};