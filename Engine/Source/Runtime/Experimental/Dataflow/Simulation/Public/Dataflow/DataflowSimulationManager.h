// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Interface.h"
#include "Subsystems/WorldSubsystem.h"
#include "Templates/SharedPointer.h"
#include "Dataflow/DataflowSimulationContext.h"

#include "DataflowSimulationManager.generated.h"

struct FDataflowSimulationProxy;
class IDataflowSimulationInterface;

namespace UE::Dataflow
{
	namespace Private
	{
		/** Per dataflow graph simulation data type (data interfaces + simulation context) */
		struct  FDataflowSimulationData
		{
			/** List of all simulation interfaces used in this this dataflow graph */
			TMap<FString, TSet<IDataflowSimulationInterface*>> SimulationInterfaces;

			/** Simulation context used to evaluate the graph on PT */
			TSharedPtr<UE::Dataflow::FDataflowSimulationContext> SimulationContext;

			/** Check is there is any datas to process */
			bool IsEmpty() const
			{
				for(const TPair<FString, TSet<IDataflowSimulationInterface*>>& InterfacesPair : SimulationInterfaces)
				{
					if(!InterfacesPair.Value.IsEmpty())
					{
						return false;
					}
				}
				return true;
			}
		};
	}
	DATAFLOWSIMULATION_API void RegisterSimulationInterface(const TObjectPtr<UObject>& SimulationObject);
	DATAFLOWSIMULATION_API void UnregisterSimulationInterface(const TObjectPtr<UObject>& SimulationObject);
}

UCLASS()
class DATAFLOWSIMULATION_API UDataflowSimulationManager : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	UDataflowSimulationManager();
	virtual ~UDataflowSimulationManager() override  = default;

	/** Static function to add world delegates */
	static void OnStartup();
	
	/** Static function to remove world delegates */
	static void OnShutdown();

	// Begin FTickableGameObject overrides
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickableInEditor() const override { return true; }
	virtual ETickableTickType GetTickableTickType() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual TStatId GetStatId() const override;
	// End FTickableGameObject overrides
	
	// Begin USubsystem overrides
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	// End USubsystem overrides

	/** Advance in time the registered simulation data (PT) */
    void AdvanceSimulationProxies(const float DeltaTime, const float SimulationTime);

	/** Check if the manager has a simulation interface */
	bool HasSimulationInterface(const IDataflowSimulationInterface* SimulationInterface) const;

	/** Add a dataflow simulation interface to the manager*/
	void AddSimulationInterface(IDataflowSimulationInterface* SimulationInterface);

	/** Remove a dataflow simulation interface from the manager */
	void RemoveSimulationInterface(const IDataflowSimulationInterface* SimulationInterface);

	/** Read the simulation interfaces and Write all the data to the simulation proxies (to be send from GT->PT) */
	void ReadSimulationInterfaces(const float DeltaTime, const bool bAsyncTask);

	/** Read all the data from the simulation proxies and write the result onto the interfaces (received from PT->GT) */
	void WriteSimulationInterfaces(const float DeltaTime, const bool bAsyncTask);

	/** Init all the simulation interfaces*/
	void InitSimulationInterfaces();

	/** Reset all the simulation interfaces*/
	void ResetSimulationInterfaces();

	/** Complete all the simulation tasks */
	void CompleteSimulationTasks();

	/** Start the simulation tasks given a delta time */
	void StartSimulationTasks(const float DeltaTime, const float SimulationTime);

	/** Set the simulation flag to enable/disable the simulation */
	void SetSimulationEnabled(const bool bSimulationEnabled) {bIsSimulationEnabled = bSimulationEnabled;}

	/** Set the simulation scene stepping bool */
	void SetSimulationStepping(const bool bSimulationStepping) {bStepSimulationScene = bSimulationStepping;}

	/** Get the simulation context for a given asset */
	TSharedPtr<UE::Dataflow::FDataflowSimulationContext> GetSimulationContext(const TObjectPtr<UDataflow>& DataflowAsset) const;
	
private :

	/** Pre process before the simulation step */
	void PreProcessSimulation(const float DeltaTime);

	/** Post process after the simulation step */
	void PostProcessSimulation(const float DeltaTime);

	/** static delegate for object property changed */
	static FDelegateHandle OnObjectPropertyChangedHandle;

	/** static delegate for post actor tick */
	static FDelegateHandle OnWorldPostActorTick;

	/** static delegate for physics state creation */
	static FDelegateHandle OnCreatePhysicsStateHandle;

	/** static delegate for physics state destruction */
	static FDelegateHandle OnDestroyPhysicsStateHandle;

	/** Dataflow simulation data registered to the manager */
	TMap<TObjectPtr<UDataflow>, UE::Dataflow::Private::FDataflowSimulationData> SimulationData;
	
	/** Simulation tasks in which the graph will be evaluated */
	TArray<FGraphEventRef> SimulationTasks;
 
	/** Boolean to control if the simulation should be disabled or not */
	bool bIsSimulationEnabled = true;
	
	/** Boolean to check if we are stepping the simulation scene */
	bool bStepSimulationScene = false;
};

/** Dataflow simulation actor interface to be able to call BP events before/after the manager ticking in case we need it */
UINTERFACE(MinimalAPI, Blueprintable)
class UDataflowSimulationActor : public UInterface
{
	GENERATED_BODY()
};

class IDataflowSimulationActor
{
	GENERATED_BODY()
 
public:
	/** Pre simulation callback function that can be implemented in C++ or Blueprint. */
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Dataflow")
	void PreDataflowSimulationTick(const float SimulationTime, const float DeltaTime);

	/** Post simulation callback function that can be implemented in C++ or Blueprint. */
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Dataflow")
	void PostDataflowSimulationTick(const float SimulationTime, const float DeltaTime);
};
