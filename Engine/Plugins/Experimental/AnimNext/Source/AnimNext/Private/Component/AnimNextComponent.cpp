// Copyright Epic Games, Inc. All Rights Reserved.

#include "Component/AnimNextComponent.h"

#include "AnimNextComponentWorldSubsystem.h"
#include "Blueprint/BlueprintExceptionInfo.h"
#include "Engine/World.h"
#include "Module/ModuleTaskContext.h"
#include "Module/ProxyVariablesContext.h"

void UAnimNextComponent::OnRegister()
{
	using namespace UE::AnimNext;

	Super::OnRegister();

	Subsystem = GetWorld()->GetSubsystem<UAnimNextComponentWorldSubsystem>();

	if (Subsystem && Module)
	{
		check(!ModuleHandle.IsValid());

		CreatePublicVariablesProxy();
		Subsystem->Register(this);
	}
}

void UAnimNextComponent::OnUnregister()
{
	Super::OnUnregister();

	if(Subsystem)
	{
		Subsystem->Unregister(this);
		Subsystem = nullptr;
		DestroyPublicVariablesProxy();
	}
}

void UAnimNextComponent::BeginPlay()
{
	Super::BeginPlay();

	SetEnabled(true);
}

void UAnimNextComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	SetEnabled(false);
}

#if WITH_EDITOR
void UAnimNextComponent::OnModuleCompiled()
{
	CreatePublicVariablesProxy();
}
#endif

void UAnimNextComponent::CreatePublicVariablesProxy()
{
	PublicVariablesProxyMap.Reset();
	PublicVariablesProxy.Reset();
	if(Module && Module->GetPublicVariableDefaults().GetPropertyBagStruct())
	{
		PublicVariablesProxy.Data = Module->GetPublicVariableDefaults();
		TConstArrayView<FPropertyBagPropertyDesc> ProxyDescs = PublicVariablesProxy.Data.GetPropertyBagStruct()->GetPropertyDescs();
		PublicVariablesProxyMap.Reserve(ProxyDescs.Num());
		for(int32 DescIndex = 0; DescIndex < ProxyDescs.Num(); ++DescIndex)
		{
			PublicVariablesProxyMap.Add(ProxyDescs[DescIndex].Name, DescIndex);
		}
		PublicVariablesProxy.DirtyFlags.SetNum(ProxyDescs.Num(), false);
	}
}

void UAnimNextComponent::DestroyPublicVariablesProxy()
{
	PublicVariablesProxyMap.Empty();
	PublicVariablesProxy.Empty();
}

void UAnimNextComponent::FlipPublicVariablesProxy(const UE::AnimNext::FProxyVariablesContext& InContext)
{
	FRWScopeLock Lock(PublicVariablesLock, SLT_Write);
	Swap(InContext.GetPublicVariablesProxy(), PublicVariablesProxy);
}

void UAnimNextComponent::SetVariable(FName Name, int32 Value)
{
	checkNoEntry();
}

DEFINE_FUNCTION(UAnimNextComponent::execSetVariable)
{
	using namespace UE::AnimNext;

	// Read wildcard Value input.
	Stack.MostRecentPropertyAddress = nullptr;
	Stack.MostRecentPropertyContainer = nullptr;

	P_GET_PROPERTY(FNameProperty, Name);

	Stack.StepCompiledIn<FProperty>(nullptr);
	const FProperty* ValueProp = CastField<FProperty>(Stack.MostRecentProperty);
	const void* ContainerPtr = Stack.MostRecentPropertyContainer;

	P_FINISH;

	if (!ValueProp || !ContainerPtr)
	{
		FBlueprintExceptionInfo ExceptionInfo(
			EBlueprintExceptionType::AbortExecution,
			NSLOCTEXT("AnimNextComponent", "AnimNextComponent_SetVariableError", "Failed to resolve the Value for Set Variable")
		);

		FBlueprintCoreDelegates::ThrowScriptException(P_THIS, Stack, ExceptionInfo);
		return;
	}

	if (Name == NAME_None)
	{
		FBlueprintExceptionInfo ExceptionInfo(
			EBlueprintExceptionType::NonFatalError,
			NSLOCTEXT("AnimNextComponent", "AnimNextComponent_SetVariableInvalidWarning", "Invalid variable name supplied to Set Variable")
		);

		FBlueprintCoreDelegates::ThrowScriptException(P_THIS, Stack, ExceptionInfo);
		return;
	}

	int32* IndexPtr = P_THIS->PublicVariablesProxyMap.Find(Name);
	if (IndexPtr == nullptr)
	{
		FBlueprintExceptionInfo ExceptionInfo(
			EBlueprintExceptionType::NonFatalError,
			FText::Format(NSLOCTEXT("AnimNextComponent", "AnimNextComponent_SetVariableNotFoundWarning", "Unknown variable name '{0}' supplied to Set Variable"), FText::FromName(Name))
		);

		FBlueprintCoreDelegates::ThrowScriptException(P_THIS, Stack, ExceptionInfo);
		return;
	}

	P_NATIVE_BEGIN;

	{
		FRWScopeLock Lock(P_THIS->PublicVariablesLock, SLT_Write);
		TConstArrayView<FPropertyBagPropertyDesc> ProxyDescs = P_THIS->PublicVariablesProxy.Data.GetPropertyBagStruct()->GetPropertyDescs();
		const void* ValuePtr = ValueProp->ContainerPtrToValuePtr<void>(ContainerPtr);
		ProxyDescs[*IndexPtr].CachedProperty->SetValue_InContainer(P_THIS->PublicVariablesProxy.Data.GetMutableValue().GetMemory(), ValuePtr);
		P_THIS->PublicVariablesProxy.DirtyFlags[*IndexPtr] = true;
		P_THIS->PublicVariablesProxy.bIsDirty = true;
	}

	P_NATIVE_END;
}

void UAnimNextComponent::SetEnabled(bool bEnabled)
{
	if(Subsystem)
	{
		Subsystem->SetEnabled(this, bEnabled);
	}
}

void UAnimNextComponent::QueueInputTraitEvent(FAnimNextTraitEventPtr Event)
{
	using namespace UE::AnimNext;

	if(Subsystem)
	{
		Subsystem->QueueTask(this, NAME_None, [Event = MoveTemp(Event)](const FModuleTaskContext& InContext)
		{
			InContext.QueueInputTraitEvent(Event);
		},
		ETaskRunLocation::After);
	}
}
