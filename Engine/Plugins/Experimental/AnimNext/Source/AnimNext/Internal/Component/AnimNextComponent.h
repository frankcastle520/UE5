// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AnimNextPublicVariablesProxy.h"
#include "Components/ActorComponent.h"
#include "Module/AnimNextModule.h"
#include "Module/ModuleHandle.h"
#include "Param/ParamType.h"
#include "TraitCore/TraitEvent.h"
#include "Variables/IAnimNextVariableProxyHost.h"

#include "AnimNextComponent.generated.h"

struct FAnimNextComponentInstanceData;
class UAnimNextComponentWorldSubsystem;

namespace UE::AnimNext
{
	struct FProxyVariablesContext;
};

namespace UE::AnimNext::UncookedOnly
{
	struct FUtils;
}

UCLASS(MinimalAPI, meta = (BlueprintSpawnableComponent))
class UAnimNextComponent : public UActorComponent, public IAnimNextVariableProxyHost
{
	GENERATED_BODY()

	// UActorComponent interface
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// IAnimNextVariableProxyHost interface
	virtual void FlipPublicVariablesProxy(const UE::AnimNext::FProxyVariablesContext& InContext) override;

#if WITH_EDITOR
	// Called back to refresh any cached data on module compilation
	void OnModuleCompiled();
#endif

	// (Re-)create the public variable proxy
	void CreatePublicVariablesProxy();

	// (Re-)create the public variable proxy
	void DestroyPublicVariablesProxy();

public:
	// Sets a module variable's value.
	// @param    Name     The name of the variable to set
	// @param    Value    The value to set the variable to
	UFUNCTION(BlueprintCallable, Category = "AnimNext", CustomThunk, meta = (CustomStructureParam = Value, UnsafeDuringActorConstruction))
	ANIMNEXT_API void SetVariable(UPARAM(meta = (CustomWidget = "VariableName")) FName Name, int32 Value);

	// Enable or disable this component's update
	UFUNCTION(BlueprintCallable, Category = "AnimNext")
	ANIMNEXT_API void SetEnabled(bool bEnabled);

	// Queues an input trait event
	// Input events will be processed in the next graph update after they are queued
	ANIMNEXT_API void QueueInputTraitEvent(FAnimNextTraitEventPtr Event);

private:
	DECLARE_FUNCTION(execSetVariable);

private:
	friend struct UE::AnimNext::UncookedOnly::FUtils;
	friend class UAnimNextComponentWorldSubsystem;

	// The AnimNext module that this component will run
	UPROPERTY(EditAnywhere, Category="Module")
	TObjectPtr<UAnimNextModule> Module = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UAnimNextComponentWorldSubsystem> Subsystem = nullptr;
	
	// Handle to the registered module
	UE::AnimNext::FModuleHandle ModuleHandle;

	// Lock for public variables proxy
	FRWLock PublicVariablesLock;

	// Proxy public variables
	UPROPERTY()
	FAnimNextPublicVariablesProxy PublicVariablesProxy;

	// Map from name->proxy variable index
	TMap<FName, int32> PublicVariablesProxyMap;

	// How to initialize the module
	UPROPERTY(EditAnywhere, Category="Module")
	EAnimNextModuleInitMethod InitMethod = EAnimNextModuleInitMethod::InitializeAndPauseInEditor;
};
