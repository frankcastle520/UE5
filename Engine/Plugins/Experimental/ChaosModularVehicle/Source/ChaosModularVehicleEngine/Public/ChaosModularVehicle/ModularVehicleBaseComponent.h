// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ChaosModularVehicle/ChaosSimModuleManagerAsyncCallback.h"
#include "SimModule/SimModulesInclude.h"
#include "SimModule/ModuleInput.h"
#include "ChaosModularVehicle/InputProducer.h"
#include "ChaosModularVehicle/ModularVehicleSimulationCU.h"
#include "Chaos/ParticleHandleFwd.h"
#include "Components/PrimitiveComponent.h"
#include "Components/ActorComponent.h"
#include "Containers/Map.h"
#include "UObject/ObjectKey.h"

#include "ModularVehicleBaseComponent.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogModularBase, Log, All);

struct FModularVehicleAsyncInput;
struct FChaosSimModuleManagerAsyncOutput;
class UClusterUnionComponent;
namespace Chaos
{
	class FSimTreeUpdates;
}

USTRUCT()
struct FVehicleComponentData
{
	GENERATED_BODY()

	int Guid = -1;
};

/** Additional replicated state */
USTRUCT()
struct CHAOSMODULARVEHICLEENGINE_API FModularReplicatedState : public FModularVehicleInputs
{
	GENERATED_USTRUCT_BODY()

	FModularReplicatedState() : FModularVehicleInputs()
	{
	}

};


USTRUCT()
struct CHAOSMODULARVEHICLEENGINE_API FConstructionData
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	TObjectPtr<UPrimitiveComponent> Component = nullptr;

	UPROPERTY()
	int32 ConstructionIndex = INDEX_NONE;
};


USTRUCT()
struct CHAOSMODULARVEHICLEENGINE_API FModuleAnimationSetup
{
	GENERATED_USTRUCT_BODY()

	FModuleAnimationSetup(FName BoneNameIn)
		: BoneName(BoneNameIn)
		, RotOffset(FRotator::ZeroRotator)
		, LocOffset(FVector::ZeroVector)
		, AnimFlags(0)
	{
	}

	FModuleAnimationSetup() 
		: BoneName(NAME_None)
		, RotOffset(FRotator::ZeroRotator)
		, LocOffset(FVector::ZeroVector)
		, AnimFlags(0)
	{
	}

	FName BoneName;
	FRotator RotOffset;
	FVector LocOffset;
	uint16 AnimFlags;
};


UCLASS(ClassGroup = (Physics), meta = (BlueprintSpawnableComponent), hidecategories = (PlanarMovement, "Components|Movement|Planar", Activation, "Components|Activation"))
class CHAOSMODULARVEHICLEENGINE_API UModularVehicleBaseComponent : public UPawnMovementComponent
{
	GENERATED_UCLASS_BODY()

	~UModularVehicleBaseComponent();

	friend struct FModularVehicleAsyncInput;
	friend struct FModularVehicleAsyncOutput;

	friend struct FNetworkModularVehicleInputs;
	friend struct FNetworkModularVehicleStates;

	friend class FModularVehicleManager;
	friend class FChaosSimModuleManagerAsyncCallback;

	friend class FModularVehicleBuilder;
public:

	using FInputNameMap = TMap<FName, int>;

	APlayerController* GetPlayerController() const;
	bool IsLocallyControlled() const;
	void SetTreeProcessingOrder(ESimTreeProcessingOrder TreeProcessingOrderIn) { TreeProcessingOrder = TreeProcessingOrderIn; }
	ESimTreeProcessingOrder GetTreeProcessingOrder() { return TreeProcessingOrder; }

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	virtual bool ShouldCreatePhysicsState() const override { return true; }
	virtual void OnCreatePhysicsState() override;
	virtual void OnDestroyPhysicsState() override;
	virtual void SetClusterComponent(UClusterUnionComponent* InPhysicalComponent);

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	void ProduceInput(int32 PhysicsStep, int32 NumSteps);

	void CreateAssociatedSimComponents(USceneComponent* AttachedComponent, int ParentIndex, int TransformIndex, Chaos::FSimTreeUpdates& TreeUpdatesOut);

	void PreTickGT(float DeltaTime);
	void UpdateState(float DeltaTime);
	TUniquePtr<FModularVehicleAsyncInput> SetCurrentAsyncData(int32 InputIdx, FChaosSimModuleManagerAsyncOutput* CurOutput, FChaosSimModuleManagerAsyncOutput* NextOutput, float Alpha, int32 VehicleManagerTimestamp);

	void ParallelUpdate(float DeltaTime);
	void Update(float DeltaTime);
	void FinalizeSimCallbackData(FChaosSimModuleManagerAsyncInput& Input);

	/** handle stand-alone and networked mode control inputs */
	void ProcessControls(float DeltaTime);

	void ShowDebugInfo(AHUD* HUD, UCanvas* Canvas, const FDebugDisplayInfo& DisplayInfo, float& YL, float& YPos);

	TUniquePtr<FPhysicsVehicleOutput>& PhysicsVehicleOutput()
	{
		return PVehicleOutput;
	}

	FORCEINLINE const FTransform& GetComponentTransform() const;

	/** Use to naturally decelerate linear velocity of objects */
	UPROPERTY(EditAnywhere, Category = "Game|Components|ModularVehicle")
	float LinearDamping;

	/** Use to naturally decelerate angular velocity of objects */
	UPROPERTY(EditAnywhere, Category = "Game|Components|ModularVehicle")
	float AngularDamping;

	UPROPERTY(EditAnywhere, Category = "Game|Components|ModularVehicle")
	struct FCollisionResponseContainer SuspensionTraceCollisionResponses;

	UPROPERTY(EditAnywhere, Category = "Game|Components|ModularVehicle")
	bool bSuspensionTraceComplex;

	/** Wheel suspension trace type, defaults to ray trace */
	UPROPERTY(EditAnywhere, Category = "Game|Components|ModularVehicle")
	ETraceType TraceType;

	UPROPERTY(EditAnywhere, Category = "Game|Components|ModularVehicle")
	bool bKeepVehicleAwake;

	/** Adds any associated simulation components to the ModularVehicleSimulation */
	UFUNCTION()
	void AddComponentToSimulation(UPrimitiveComponent* Component, const TArray<FClusterUnionBoneData>& BonesData, const TArray<FClusterUnionBoneData>& RemovedBoneIDs, bool bIsNew);

	/** Removes any associated simulation components from the ModularVehicleSimulation */
	UFUNCTION()
	void RemoveComponentFromSimulation(UPrimitiveComponent* Component, const TArray<FClusterUnionBoneData>& RemovedBonesData);

	UFUNCTION(BlueprintCallable, Category = "Game|Components|ModularVehicle")
	void SetLocallyControlled(bool bLocallyControlledIn);

	UPROPERTY(EditAnywhere, Category = "Game|Components|ModularVehicle")
	TSubclassOf<UVehicleInputProducerBase> InputProducerClass;

	// CONTROLS
	// 
	//void SetInput(const FName& Name, const FModuleInputValue& Value);
	void SetInput(const FName& Name, const bool Value);
	void SetInput(const FName& Name, const double Value);
	void SetInput(const FName& Name, const FVector2D& Value);
	void SetInput(const FName& Name, const FVector& Value);

	// Sets the input producer class and creates an input producer if one doesn't exist.
	UFUNCTION(BlueprintCallable, Category = "Game|Components|ModularVehicle")
	void SetInputProducerClass(TSubclassOf<UVehicleInputProducerBase> InInputProducerClass);

	TObjectPtr<UVehicleInputProducerBase> GetInputProducer() { return InputProducer; }

	UFUNCTION(BlueprintCallable, Category = "Game|Components|ModularVehicle")
	void SetInputBool(const FName Name, const bool Value);

	UFUNCTION(BlueprintCallable, Category = "Game|Components|ModularVehicle")
	void SetInputAxis1D(const FName Name, const double Value);

	UFUNCTION(BlueprintCallable, Category = "Game|Components|ModularVehicle")
	void SetInputAxis2D(const FName Name, const FVector2D Value);

	UFUNCTION(BlueprintCallable, Category = "Game|Components|ModularVehicle")
	void SetInputAxis3D(const FName Name, const FVector Value);

	/** Set the gear directly */
	UFUNCTION(BlueprintCallable, Category = "Game|Components|ModularVehicle")
	void SetGearInput(int32 Gear);

	UFUNCTION(BlueprintCallable, Category = "Game|Components|ModularVehicle")
	int32 GetCurrentGear();

	UFUNCTION(BlueprintCallable, Category = "Game|Components|ModularVehicle")
	bool IsReversing();

	UFUNCTION(BlueprintCallable, Category = "Game|Components|ModularVehicle")
	void AddActorsToIgnore(TArray<AActor*>& ActorsIn);

	UFUNCTION(BlueprintCallable, Category = "Game|Components|ModularVehicle")
	void RemoveActorsToIgnore(TArray<AActor*>& ActorsIn);

	// Bypass the need for a controller in order for the controls to be processed.
	UPROPERTY(EditAnywhere, Category = "Game|Components|ModularVehicle")
	bool bRequiresControllerForInputs;

	/** Grab nearby components and add them to the cluster union representing the vehicle */
	UPROPERTY(EditAnywhere, Category = "Game|Components|ModularVehicle")
	bool bAutoAddComponentsFromWorld;

	/** The size of the overlap box testing for nearby components in the world  */
	UPROPERTY(EditAnywhere, Category = "Game|Components|ModularVehicle", meta = (EditCondition = "bAutoAddComponentsToCluster"))
	FVector AutoAddOverlappingBoxSize;

	UPROPERTY(EditAnywhere, Category = "Game|Components|ModularVehicle", meta = (EditCondition = "bAutoAddComponentsToCluster"))
	int32 DelayClusteringCount;

	/*** Map simulation component to our vehicle setup data */
	TMap<TObjectKey<USceneComponent>, FVehicleComponentData> ComponentToPhysicsObjects;

	UClusterUnionComponent* ClusterUnionComponent;

	/** Set all channels to the specified response - for wheel raycasts */
	void SetWheelTraceAllChannels(ECollisionResponse NewResponse)
	{
		SuspensionTraceCollisionResponses.SetAllChannels(NewResponse);
	}

	/** Set the response of this body to the supplied settings - for wheel raycasts */
	void SetWheelTraceResponseToChannel(ECollisionChannel Channel, ECollisionResponse NewResponse)
	{
		SuspensionTraceCollisionResponses.SetResponse(Channel, NewResponse);
	}

	TArray<FModuleAnimationSetup>& AccessModuleAnimationSetups() { return ModuleAnimationSetups; }
	const TArray<FModuleAnimationSetup>& GetModuleAnimationSetups() const { return ModuleAnimationSetups; }

protected:

	void CreateVehicleSim();
	void DestroyVehicleSim();
	void UpdatePhysicalProperties();
	void AddOverlappingComponentsToCluster();
	void AddGeometryCollectionsFromOwnedActor();
	void SetupSkeletalAnimationStructure();
	void AssimilateComponentInputs(TArray<FModuleInputSetup>& OutCombinedInputs);
	// #TODO reinstate? virtual void GenerateInputModifiers(const TArray<FModuleInputSetup>& CombinedInputConfiguration);
	// #TODO reinstate? virtual void ApplyInputModifiers(float DeltaTime, const FModuleInputContainer& RawValue);


	void ActionTreeUpdates(Chaos::FSimTreeUpdates* NextTreeUpdates);

	void SetCurrentAsyncDataInternal(FModularVehicleAsyncInput* CurInput, int32 InputIdx, FChaosSimModuleManagerAsyncOutput* CurOutput, FChaosSimModuleManagerAsyncOutput* NextOutput, float Alpha, int32 VehicleManagerTimestamp);
	int32 FindParentsLastSimComponent(const USceneComponent* AttachedComponent);

	IPhysicsProxyBase* GetPhysicsProxy() const;


	int32 FindComponentAddOrder(UPrimitiveComponent* InComponent);
	bool FindAndRemoveNextPendingUpdate(int32 NextIndex, Chaos::FSimTreeUpdates* OutData);

	// replicated state of vehicle 
	UPROPERTY(Transient, Replicated)
	FModularReplicatedState ReplicatedState;

	// latest gear selected
	UPROPERTY(Transient)
	int32 GearInput;

	// The currently selected gear
	UPROPERTY(Transient)
	int32 CurrentGear;

	// The engine RPM
	UPROPERTY(Transient)
	float EngineRPM;

	// The engine Torque
	UPROPERTY(Transient)
	float EngineTorque;

	UPROPERTY()
	TObjectPtr<UNetworkPhysicsComponent> NetworkPhysicsComponent = nullptr;

public:

	UPROPERTY(EditAnywhere, Category = VehicleInput)
	TArray<FModuleInputSetup> InputConfig;
		
	UPROPERTY(EditAnywhere, Category = "Game|Components|ModularVehicle")
	TEnumAsByte<ESimTreeProcessingOrder> TreeProcessingOrder = ESimTreeProcessingOrder::LeafFirst;

	UPROPERTY(Transient, Replicated)
	TArray<FConstructionData> ConstructionDatas;

	/** Pass current state to server */
	UFUNCTION(reliable, server, WithValidation)
	void ServerUpdateState(const FModuleInputContainer& InputsIn, bool KeepAwake);

	void LogInputSetup();

	TArray<AActor*> ActorsToIgnore;
	EChaosAsyncVehicleDataType CurAsyncType;
	FModularVehicleAsyncInput* CurAsyncInput;
	struct FModularVehicleAsyncOutput* CurAsyncOutput;
	struct FModularVehicleAsyncOutput* NextAsyncOutput;
	float OutputInterpAlpha;

	struct FAsyncOutputWrapper
	{
		int32 Idx;
		int32 Timestamp;

		FAsyncOutputWrapper()
			: Idx(INDEX_NONE)
			, Timestamp(INDEX_NONE)
		{
		}
	};

	TArray<FAsyncOutputWrapper> OutputsWaitingOn;
	TUniquePtr<FPhysicsVehicleOutput> PVehicleOutput;	/* physics simulation data output from the async physics thread */
	TUniquePtr<FModularVehicleSimulationCU> VehicleSimulationPT;	/* simulation code running on the physics thread async callback */

private:

	int NextTransformIndex = 0; // is there a better way, getting from size of map/array?
	UPrimitiveComponent* MyComponent = nullptr;

	bool bUsingNetworkPhysicsPrediction = false;
	float PrevSteeringInput = 0.0f;

	int32 LastComponentAddIndex = INDEX_NONE;
	TMap<TObjectKey<UPrimitiveComponent>, Chaos::FSimTreeUpdates> PendingTreeUpdates;

	int32 NextConstructionIndex = 0;

	int32 ClusteringCount = 0;

	bool bIsLocallyControlled;

	TArray<FModuleAnimationSetup> ModuleAnimationSetups;

	FInputNameMap InputNameMap;	// map input name to input container array index

	UPROPERTY(Transient)
	TObjectPtr<UVehicleInputProducerBase> InputProducer = nullptr;

	FModuleInputContainer InputsContainer;
};
