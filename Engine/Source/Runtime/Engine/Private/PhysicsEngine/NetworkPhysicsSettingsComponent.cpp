// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	NetworkPhysicsSettingsComponent.cpp
	Handles data distribution of networked physics settings to systems that need it, on both Game-Thread and Physics-Thread.
=============================================================================*/

#include "Physics/NetworkPhysicsSettingsComponent.h"
#include "Engine/Engine.h"
#include "Chaos/Declares.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "PBDRigidsSolver.h"
#include "PhysicsReplication.h"
#include "Components/PrimitiveComponent.h"

namespace PhysicsReplicationCVars
{
	namespace ResimulationCVars
	{
		int32 SimProxyRepMode = -1;
		static FAutoConsoleVariableRef CVarSimProxyRepMode(TEXT("np2.Resim.SimProxyRepMode"), SimProxyRepMode, TEXT("All actors with a NetworkPhysicsSettingsComponent and that are running resimulation and is ROLE_SimulatedProxy will change their physics replication mode. -1 = Disabled, 0 = Default, 1 = PredictiveInterpolation, 2 = Resimulation"));
	}
}


TMap<AActor*, UNetworkPhysicsSettingsComponent*> UNetworkPhysicsSettingsComponent::ObjectToSettings_External = TMap<AActor*, UNetworkPhysicsSettingsComponent*>();

UNetworkPhysicsSettingsComponent::UNetworkPhysicsSettingsComponent()
{
	bWantsInitializeComponent = true;
	bAutoActivate = true;
}

void UNetworkPhysicsSettingsComponent::InitializeComponent()
{
	Super::InitializeComponent();

	using namespace Chaos;
	NetworkPhysicsSettings_Internal = nullptr;
	if (UWorld* World = GetWorld())
	{
		if (FPhysScene* PhysScene = World->GetPhysicsScene())
		{
			if (Chaos::FPhysicsSolver* Solver = PhysScene->GetSolver())
			{
				NetworkPhysicsSettings_Internal = Solver->CreateAndRegisterSimCallbackObject_External<FNetworkPhysicsSettingsComponentAsync>();
				
				// Marshal settings data from GT to PT
				if (NetworkPhysicsSettings_Internal)
				{
					if (AActor* Owner = GetOwner())
					{
						if (UPrimitiveComponent* RootPrimComp = Cast<UPrimitiveComponent>(Owner->GetRootComponent()))
						{
							if (Chaos::FConstPhysicsObjectHandle PhysicsObject = RootPrimComp->GetPhysicsObjectByName(NAME_None))
							{
								FNetworkPhysicsSettingsAsyncInput* AsyncInput = NetworkPhysicsSettings_Internal->GetProducerInputData_External();
								AsyncInput->PhysicsObject = PhysicsObject;
								AsyncInput->Settings.GeneralSettings = GeneralSettings;
								AsyncInput->Settings.DefaultReplicationSettings = DefaultReplicationSettings;
								AsyncInput->Settings.ResimulationSettings = ResimulationSettings;
								AsyncInput->Settings.PredictiveInterpolationSettings = PredictiveInterpolationSettings;
								AsyncInput->Settings.NetworkPhysicsComponentSettings = NetworkPhysicsComponentSettings;
							}
						}
					}
				}
			}
		}
	}

	if (AActor* Owner = GetOwner())
	{
		UNetworkPhysicsSettingsComponent::ObjectToSettings_External.Add(Owner, this);
	}
}

void UNetworkPhysicsSettingsComponent::UninitializeComponent()
{
	Super::UninitializeComponent();

	using namespace Chaos;
	if (UWorld* World = GetWorld())
	{
		if (FPhysScene* PhysScene = World->GetPhysicsScene())
		{
			if (Chaos::FPhysicsSolver* Solver = PhysScene->GetSolver())
			{
				Solver->UnregisterAndFreeSimCallbackObject_External(NetworkPhysicsSettings_Internal);
			}
		}
	}
	NetworkPhysicsSettings_Internal = nullptr;

	if (AActor* Owner = GetOwner())
	{
		UNetworkPhysicsSettingsComponent::ObjectToSettings_External.Remove(Owner);
	}
}

void UNetworkPhysicsSettingsComponent::BeginPlay()
{
	Super::BeginPlay();

	// Apply overrides on actor
	if (AActor* Owner = GetOwner())
	{
		if ((GeneralSettings.bOverrideSimProxyRepMode || PhysicsReplicationCVars::ResimulationCVars::SimProxyRepMode >= 0)
			&& Owner->GetLocalRole() == ENetRole::ROLE_SimulatedProxy)
		{
			EPhysicsReplicationMode RepMode = GeneralSettings.bOverrideSimProxyRepMode ? GeneralSettings.SimProxyRepMode : static_cast<EPhysicsReplicationMode>(PhysicsReplicationCVars::ResimulationCVars::SimProxyRepMode);
			Owner->SetPhysicsReplicationMode(RepMode);
		}
	}
}

UNetworkPhysicsSettingsComponent* UNetworkPhysicsSettingsComponent::GetSettingsForActor(AActor* Owner)
{
	UNetworkPhysicsSettingsComponent** Value = ObjectToSettings_External.Find(Owner);
	return Value ? *Value : nullptr;
}

#pragma region // FNetworkPhysicsSettingsComponentAsync

void FNetworkPhysicsSettingsComponentAsync::OnPostInitialize_Internal()
{
	// Receive settings data on PT from GT
	if (const FNetworkPhysicsSettingsAsyncInput* AsyncInput = GetConsumerInput_Internal())
	{
		Settings = AsyncInput->Settings;
		
		if (Chaos::FPBDRigidsSolver* RigidsSolver = static_cast<Chaos::FPBDRigidsSolver*>(GetSolver()))
		{
			if (FPhysicsReplicationAsync* PhysRep = RigidsSolver->GetPhysicsReplication())
			{
				PhysRep->RegisterSettings(AsyncInput->PhysicsObject, Settings);
			}
		}
	}
}

#pragma endregion // FNetworkPhysicsSettingsComponentAsync
