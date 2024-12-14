// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosModularVehicle/ChaosSimModuleManagerAsyncCallback.h"

#include "ChaosModularVehicle/ModularVehicleBaseComponent.h"
#include "ChaosModularVehicle/ModularVehicleSimulationCU.h"
#include "PBDRigidsSolver.h"
#include "Chaos/ParticleHandleFwd.h"
#include "PhysicsProxy/GeometryCollectionPhysicsProxy.h"
#include "PhysicsProxy/ClusterUnionPhysicsProxy.h"
#include "SimModule/ModuleFactoryRegister.h"

FSimModuleDebugParams GSimModuleDebugParams;

DECLARE_CYCLE_STAT(TEXT("AsyncCallback:OnPreSimulate_Internal"), STAT_AsyncCallback_OnPreSimulate, STATGROUP_ChaosSimModuleManager);
DECLARE_CYCLE_STAT(TEXT("AsyncCallback:OnContactModification_Internal"), STAT_AsyncCallback_OnContactModification, STATGROUP_ChaosSimModuleManager);

FName FChaosSimModuleManagerAsyncCallback::GetFNameForStatId() const
{
	const static FLazyName StaticName("FChaosSimModuleManagerAsyncCallback");
	return StaticName;
}

/**
 * Callback from Physics thread
 */

void FChaosSimModuleManagerAsyncCallback::ProcessInputs_Internal(int32 PhysicsStep)
{
	const FChaosSimModuleManagerAsyncInput* AsyncInput = GetConsumerInput_Internal();
	if (AsyncInput == nullptr)
	{
		return;
	}

	for (const TUniquePtr<FModularVehicleAsyncInput>& VehicleInput : AsyncInput->VehicleInputs)
	{
		VehicleInput->ProcessInputs();
	}
}

/**
 * Callback from Physics thread
 */
void FChaosSimModuleManagerAsyncCallback::OnPreSimulate_Internal()
{
	using namespace Chaos;

	SCOPE_CYCLE_COUNTER(STAT_AsyncCallback_OnPreSimulate);

	float DeltaTime = GetDeltaTime_Internal();
	float SimTime = GetSimTime_Internal();

	const FChaosSimModuleManagerAsyncInput* Input = GetConsumerInput_Internal();
	if (Input == nullptr)
	{
		return;
	}

	const int32 NumVehicles = Input->VehicleInputs.Num();

	UWorld* World = Input->World.Get();	//only safe to access for scene queries
	if (World == nullptr || NumVehicles == 0)
	{
		//world is gone so don't bother, or nothing to simulate.
		return;
	}

	Chaos::FPhysicsSolver* PhysicsSolver = static_cast<Chaos::FPhysicsSolver*>(GetSolver());
	if (PhysicsSolver == nullptr)
	{
		return;
	}

	FChaosSimModuleManagerAsyncOutput& Output = GetProducerOutputData_Internal();
	Output.VehicleOutputs.AddDefaulted(NumVehicles);
	Output.Timestamp = Input->Timestamp;

	const TArray<TUniquePtr<FModularVehicleAsyncInput>>& InputVehiclesBatch = Input->VehicleInputs;
	TArray<TUniquePtr<FModularVehicleAsyncOutput>>& OutputVehiclesBatch = Output.VehicleOutputs;

	// beware running the vehicle simulation in parallel, code must remain threadsafe
	auto LambdaParallelUpdate = [World, DeltaTime, SimTime, &InputVehiclesBatch, &OutputVehiclesBatch](int32 Idx)
	{
		const FModularVehicleAsyncInput& VehicleInput = *InputVehiclesBatch[Idx];

		if (VehicleInput.Proxy == nullptr)
		{
			return;
		}

		bool bWake = false;
		OutputVehiclesBatch[Idx] = VehicleInput.Simulate(World, DeltaTime, SimTime, bWake);

	};

	bool ForceSingleThread = !GSimModuleDebugParams.EnableMultithreading;
	PhysicsParallelFor(OutputVehiclesBatch.Num(), LambdaParallelUpdate, ForceSingleThread);

	// Delayed application of forces - This is separate from Simulate because forces cannot be executed multi-threaded
	for (const TUniquePtr<FModularVehicleAsyncInput>& VehicleInput : InputVehiclesBatch)
	{
		if (VehicleInput.IsValid())
		{
			VehicleInput->ApplyDeferredForces();
		}
	}
}

/**
 * Contact modification currently unused
 */
void FChaosSimModuleManagerAsyncCallback::OnContactModification_Internal(Chaos::FCollisionContactModifier& Modifications)
{
	using namespace Chaos;

	SCOPE_CYCLE_COUNTER(STAT_AsyncCallback_OnContactModification);

	float DeltaTime = GetDeltaTime_Internal();
	float SimTime = GetSimTime_Internal();

	const FChaosSimModuleManagerAsyncInput* Input = GetConsumerInput_Internal();
	if (Input == nullptr)
	{
		return;
	}

	const int32 NumVehicles = Input->VehicleInputs.Num();

	UWorld* World = Input->World.Get();	//only safe to access for scene queries
	if (World == nullptr || NumVehicles == 0)
	{
		//world is gone so don't bother.
		return;
	}

	Chaos::FPhysicsSolver* PhysicsSolver = static_cast<Chaos::FPhysicsSolver*>(GetSolver());
	if (PhysicsSolver == nullptr)
	{
		return;
	}

	const TArray<TUniquePtr<FModularVehicleAsyncInput>>& InputVehiclesBatch = Input->VehicleInputs;

	// beware running the vehicle simulation in parallel, code must remain threadsafe
	auto LambdaParallelUpdate = [&Modifications, &InputVehiclesBatch](int32 Idx)
	{
		const FModularVehicleAsyncInput& VehicleInput = *InputVehiclesBatch[Idx];

		if (VehicleInput.Proxy == nullptr)
		{
			return;
		}

		bool bWake = false;
		VehicleInput.OnContactModification(Modifications);

	};

	bool ForceSingleThread = !GSimModuleDebugParams.EnableMultithreading;
	PhysicsParallelFor(InputVehiclesBatch.Num(), LambdaParallelUpdate, ForceSingleThread);
}


TUniquePtr<FModularVehicleAsyncOutput> FModularVehicleAsyncInput::Simulate(UWorld* World, const float DeltaSeconds, const float TotalSeconds, bool& bWakeOut) const
{
	TUniquePtr<FModularVehicleAsyncOutput> Output = MakeUnique<FModularVehicleAsyncOutput>();

	//support nullptr because it allows us to go wide on filling the async inputs
	if (Proxy == nullptr)
	{
		return Output;
	}

	if (Vehicle && Vehicle->VehicleSimulationPT)
	{
		// FILL OUTPUT DATA HERE THAT WILL GET PASSED BACK TO THE GAME THREAD
		Vehicle->VehicleSimulationPT->Simulate(World, DeltaSeconds, *this, *Output.Get(), Proxy);

		FModularVehicleAsyncOutput& OutputData = *Output.Get();
		Vehicle->VehicleSimulationPT->FillOutputState(OutputData);
	}


	Output->bValid = true;

	return MoveTemp(Output);
}

void FModularVehicleAsyncInput::OnContactModification(Chaos::FCollisionContactModifier& Modifications) const
{
	if (Vehicle && Vehicle->VehicleSimulationPT)
	{
		Vehicle->VehicleSimulationPT->OnContactModification(Modifications, Proxy);
	}
}

void FModularVehicleAsyncInput::ApplyDeferredForces() const
{
	if (Vehicle && Proxy && Vehicle->VehicleSimulationPT)
	{
		if (Proxy->GetType() == EPhysicsProxyType::ClusterUnionProxy)
		{
			Vehicle->VehicleSimulationPT->ApplyDeferredForces(static_cast<Chaos::FClusterUnionPhysicsProxy*>(Proxy));
		}
		else if (Proxy->GetType() == EPhysicsProxyType::GeometryCollectionType)
		{
			Vehicle->VehicleSimulationPT->ApplyDeferredForces(static_cast<FGeometryCollectionPhysicsProxy*>(Proxy));
		}

	}

}

void FModularVehicleAsyncInput::ProcessInputs()
{
	if (!GetVehicle())
	{
		return;
	}

	FModularVehicleSimulationCU* VehicleSim = GetVehicle()->VehicleSimulationPT.Get();

	if (VehicleSim == nullptr || !GetVehicle()->bUsingNetworkPhysicsPrediction || GetVehicle()->GetWorld() == nullptr)
	{
		return;
	}
	bool bIsResimming = false;
	if (FPhysScene* PhysScene = GetVehicle()->GetWorld()->GetPhysicsScene())
	{
		if (Chaos::FPhysicsSolver* LocalSolver = PhysScene->GetSolver())
		{
			bIsResimming = LocalSolver->GetEvolution()->IsResimming();
		}
	}

	if (GetVehicle()->IsLocallyControlled() && !bIsResimming)
	{
		VehicleSim->VehicleInputs = PhysicsInputs.NetworkInputs.VehicleInputs;
	}
	else
	{
		PhysicsInputs.NetworkInputs.VehicleInputs = VehicleSim->VehicleInputs;
	}
}

bool FNetworkModularVehicleInputs::NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
{
	FNetworkPhysicsData::SerializeFrames(Ar);

	Ar << VehicleInputs.Reverse;
	Ar << VehicleInputs.KeepAwake;

	VehicleInputs.Container.Serialize(Ar, Map, bOutSuccess);

	bOutSuccess = true;
	return bOutSuccess;
}

void FNetworkModularVehicleInputs::ApplyData(UActorComponent* NetworkComponent) const
{
	if (GSimModuleDebugParams.EnableNetworkStateData)
	{
		if (UModularVehicleBaseComponent* ModularBaseComponent = Cast<UModularVehicleBaseComponent>(NetworkComponent))
		{
			if (FModularVehicleSimulationCU* VehicleSimulation = ModularBaseComponent->VehicleSimulationPT.Get())
			{
				VehicleSimulation->VehicleInputs = VehicleInputs;
			}
		}
	}
}

void FNetworkModularVehicleInputs::BuildData(const UActorComponent* NetworkComponent)
{
	if (GSimModuleDebugParams.EnableNetworkStateData)
	{
		if (const UModularVehicleBaseComponent* ModularBaseComponent = Cast<const UModularVehicleBaseComponent>(NetworkComponent))
		{
			if (const FModularVehicleSimulationCU* VehicleSimulation = ModularBaseComponent->VehicleSimulationPT.Get())
			{
				VehicleInputs = VehicleSimulation->VehicleInputs;
			}
		}
	}
}

void FNetworkModularVehicleInputs::InterpolateData(const FNetworkPhysicsData& MinData, const FNetworkPhysicsData& MaxData)
{
	const FNetworkModularVehicleInputs& MinInput = static_cast<const FNetworkModularVehicleInputs&>(MinData);
	const FNetworkModularVehicleInputs& MaxInput = static_cast<const FNetworkModularVehicleInputs&>(MaxData);

	const float LerpFactor = (LocalFrame - MinInput.LocalFrame) / (MaxInput.LocalFrame - MinInput.LocalFrame);

	VehicleInputs.Reverse = MinInput.VehicleInputs.Reverse;
	VehicleInputs.KeepAwake = MinInput.VehicleInputs.KeepAwake;
	VehicleInputs.Container.Lerp(MinInput.VehicleInputs.Container, MaxInput.VehicleInputs.Container, LerpFactor);
}

void FNetworkModularVehicleInputs::MergeData(const FNetworkPhysicsData& FromData)
{
	const FNetworkModularVehicleInputs& FromInput = static_cast<const FNetworkModularVehicleInputs&>(FromData);
	VehicleInputs.Container.Merge(FromInput.VehicleInputs.Container);
}

bool FNetworkModularVehicleStates::NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
{
	FNetworkPhysicsData::SerializeFrames(Ar);

	int32 NumNetModules = ModuleData.Num();
	Ar << NumNetModules;
	if(Ar.IsLoading() && NumNetModules != ModuleData.Num())
	{
		ModuleData.Reserve(NumNetModules);
	}
	for (int I = 0; I < NumNetModules; I++)
	{
		if (Ar.IsLoading())
		{
			if (NumNetModules > 0)
			{
				uint32 ModuleTypeHash = 0;
				int32 SimArrayIndex = 0;
				Ar << ModuleTypeHash;
				Ar << SimArrayIndex;

				if (I >= ModuleData.Num())
				{
					if (TSharedPtr<Chaos::FModuleNetData> Data = Chaos::FModuleFactoryRegister::Get().GenerateNetData(ModuleTypeHash, SimArrayIndex))
					{
						ModuleData.Emplace(Data);
					}
				}
				if(I <= ModuleData.Num() && ModuleData[I].IsValid())
				{
					check(ModuleTypeHash == Chaos::FModuleFactoryRegister::GetModuleHash(ModuleData[I]->GetSimType()));
					ModuleData[I]->Serialize(Ar);
				}
			}
		}
		else
		{
			int ModuleTypeHash = Chaos::FModuleFactoryRegister::GetModuleHash(ModuleData[I]->GetSimType());
			
			Ar << ModuleTypeHash;
			Ar << ModuleData[I]->SimArrayIndex;
			ModuleData[I]->Serialize(Ar);
		}

	}

	return true;
}

void FNetworkModularVehicleStates::ApplyData(UActorComponent* NetworkComponent) const
{
	if (UModularVehicleBaseComponent* ModularBaseComponent = Cast<UModularVehicleBaseComponent>(NetworkComponent))
	{
		if (FModularVehicleSimulationCU* VehicleSimulation = ModularBaseComponent->VehicleSimulationPT.Get())
		{
			VehicleSimulation->AccessSimComponentTree()->SetSimState(ModuleData);
		}
	}
}

void FNetworkModularVehicleStates::BuildData(const UActorComponent* NetworkComponent)
{
	if (NetworkComponent)
	{
		if (const FModularVehicleSimulationCU* VehicleSimulation = Cast<const UModularVehicleBaseComponent>(NetworkComponent)->VehicleSimulationPT.Get())
		{
			VehicleSimulation->GetSimComponentTree()->SetNetState(ModuleData);
		}
	}
}

void FNetworkModularVehicleStates::InterpolateData(const FNetworkPhysicsData& MinData, const FNetworkPhysicsData& MaxData)
{
	const FNetworkModularVehicleStates& MinState = static_cast<const FNetworkModularVehicleStates&>(MinData);
	const FNetworkModularVehicleStates& MaxState = static_cast<const FNetworkModularVehicleStates&>(MaxData);

	const float LerpFactor = (LocalFrame - MinState.LocalFrame) / (MaxState.LocalFrame - MinState.LocalFrame);

	for (int I = 0; I < ModuleData.Num(); I++)
	{
		// if these don't match then something has gone terribly wrong
		check(ModuleData[I]->GetSimType() == MinState.ModuleData[I]->GetSimType());
		check(ModuleData[I]->GetSimType() == MaxState.ModuleData[I]->GetSimType());

		ModuleData[I]->Lerp(LerpFactor, *MinState.ModuleData[I].Get(), *MaxState.ModuleData[I].Get());
	}
}

