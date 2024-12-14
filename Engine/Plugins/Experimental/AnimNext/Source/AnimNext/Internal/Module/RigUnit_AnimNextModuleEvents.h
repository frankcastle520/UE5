// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Graph/RigUnit_AnimNextBase.h"
#include "AnimNextExecuteContext.h"
#include "AnimNextModuleInstance.h"
#include "ModuleEventTickFunctionBindings.h"
#include "RigUnit_AnimNextModuleEvents.generated.h"

/** Base schedule-level event, never instantiated */
USTRUCT(meta=(Abstract, Category="Events", NodeColor="1, 0, 0", Keywords="Begin,Update,Tick,Forward,Event"))
struct ANIMNEXT_API FRigUnit_AnimNextModuleEventBase : public FRigUnit_AnimNextBase
{
	GENERATED_BODY()

	FRigUnit_AnimNextModuleEventBase() = default;

	// FRigUnit interface
	virtual FString GetUnitLabel() const override { return GetEventName().ToString(); };
	virtual bool CanOnlyExistOnce() const override final { return true; }

	// Get the general ordering phase of this event, used for linearization
	virtual UE::AnimNext::EModuleEventPhase GetEventPhase() const { return UE::AnimNext::EModuleEventPhase::Execute; }

	// Overriden in derived classes to provide binding function
	virtual UE::AnimNext::FModuleEventBindingFunction GetBindingFunction() const { return [](const UE::AnimNext::FTickFunctionBindingContext& InContext, FTickFunction& InTickFunction){}; }

	// The execution result
	UPROPERTY(EditAnywhere, DisplayName = "Execute", Category = "Events", meta = (Output))
	FAnimNextExecuteContext ExecuteContext;
};

/** Synthetic event injected by the compiler to process any variable bindings, not user instantiated */
USTRUCT(meta=(Hidden, Category="Internal", NodeColor="1, 0, 0"))
struct ANIMNEXT_API FRigUnit_AnimNextExecuteBindings : public FRigUnit_AnimNextModuleEventBase
{
	GENERATED_BODY()

	FRigUnit_AnimNextExecuteBindings() = default;

	static inline const FLazyName EventName = FLazyName("ExecuteBindings");
	
	RIGVM_METHOD()
	virtual void Execute() override;
	virtual FName GetEventName() const override final { return EventName; }

	// FRigUnit_AnimNextModuleEventBase interface
	virtual UE::AnimNext::EModuleEventPhase GetEventPhase() const override final { return UE::AnimNext::EModuleEventPhase::PreExecute; }
	virtual UE::AnimNext::FModuleEventBindingFunction GetBindingFunction() const override final;
};

/** Schedule event called to set up a module */
USTRUCT(meta=(DisplayName="Initialize", Keywords="Setup,Startup,Create"))
struct ANIMNEXT_API FRigUnit_AnimNextInitializeEvent : public FRigUnit_AnimNextModuleEventBase
{
	GENERATED_BODY()

	static inline const FLazyName EventName = FLazyName("Initialize");

	RIGVM_METHOD()
	virtual void Execute() override;
	virtual FName GetEventName() const override final { return EventName; }

	// FRigUnit_AnimNextModuleEventBase interface
	virtual UE::AnimNext::EModuleEventPhase GetEventPhase() const override final { return UE::AnimNext::EModuleEventPhase::Initialize; }
	virtual UE::AnimNext::FModuleEventBindingFunction GetBindingFunction() const override final;
};

/** Schedule event called before world physics is updated */
USTRUCT(meta=(DisplayName="PrePhysics", Keywords="Start,Before"))
struct ANIMNEXT_API FRigUnit_AnimNextPrePhysicsEvent : public FRigUnit_AnimNextModuleEventBase
{
	GENERATED_BODY()

	static inline const FLazyName EventName = FLazyName("PrePhysics");

	RIGVM_METHOD()
	virtual void Execute() override;
	virtual FName GetEventName() const override final { return EventName; }

	// FRigUnit_AnimNextModuleEventBase interface
	virtual UE::AnimNext::EModuleEventPhase GetEventPhase() const override final { return UE::AnimNext::EModuleEventPhase::Execute; }
	virtual UE::AnimNext::FModuleEventBindingFunction GetBindingFunction() const override final;
};

/** Schedule event called after world physics is updated */
USTRUCT(meta=(DisplayName="PostPhysics", Keywords="End,After"))
struct ANIMNEXT_API FRigUnit_AnimNextPostPhysicsEvent : public FRigUnit_AnimNextModuleEventBase
{
	GENERATED_BODY()

	static inline const FLazyName EventName = FLazyName("PostPhysics");

	RIGVM_METHOD()
	virtual void Execute() override;
	virtual FName GetEventName() const override final { return EventName; }

	// FRigUnit_AnimNextModuleEventBase interface
	virtual UE::AnimNext::EModuleEventPhase GetEventPhase() const override final { return UE::AnimNext::EModuleEventPhase::Execute; }
	virtual UE::AnimNext::FModuleEventBindingFunction GetBindingFunction() const override final;
};

