// Copyright Epic Games, Inc. All Rights Reserved.

#include "MovementMode.h"
#include "MoverComponent.h"
#include "Engine/BlueprintGeneratedClass.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif


#include UE_INLINE_GENERATED_CPP_BY_NAME(MovementMode)

#define LOCTEXT_NAMESPACE "Mover"

UBaseMovementMode::UBaseMovementMode(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

}

void UBaseMovementMode::DoRegister(const FName ModeName)
{
	auto IsImplementedInBlueprint = [](const UFunction* Func) -> bool
	{
		return Func && ensure(Func->GetOuter())
			&& Func->GetOuter()->IsA(UBlueprintGeneratedClass::StaticClass());
	};

	static FName SimTickFuncName = FName(TEXT("K2_OnSimulationTick"));
	UFunction* SimulationTickFunction = GetClass()->FindFunctionByName(SimTickFuncName);
	bHasBlueprintSimulationTick = IsImplementedInBlueprint(SimulationTickFunction);

	static FName GenMoveFuncName = FName(TEXT("K2_OnGenerateMove"));
	UFunction* GenMoveFunction = GetClass()->FindFunctionByName(GenMoveFuncName);
	bHasBlueprintGenerateMove = IsImplementedInBlueprint(GenMoveFunction);

	OnRegistered(ModeName);
}

void UBaseMovementMode::DoUnregister()
{
	OnUnregistered();
}


void UBaseMovementMode::DoGenerateMove(const FMoverTickStartData& StartState, const FMoverTimeStep& TimeStep, FProposedMove& OutProposedMove) const
{
	if (bHasBlueprintGenerateMove)
	{
		OutProposedMove = K2_OnGenerateMove(StartState, TimeStep);
	}
	else
	{
		OnGenerateMove(StartState, TimeStep, OutProposedMove);
	}
}


void UBaseMovementMode::DoSimulationTick(const FSimulationTickParams& Params, FMoverTickEndData& OutputState)
{
	if (bHasBlueprintSimulationTick)
	{
		OutputState = K2_OnSimulationTick(Params);
	}
	else
	{
		OnSimulationTick(Params, OutputState);
	}
}

void UBaseMovementMode::DoActivate()
{
	if (bHasBlueprintOnActivate)
	{
		K2_OnActivate();
	}
	else
	{
		OnActivate();
	}
}

void UBaseMovementMode::DoDeactivate()
{
	if (bHasBlueprintOnDeactivate)
	{
		K2_OnDeactivate();
	}
	else
	{
		OnDeactivate();
	}
}


UMoverComponent* UBaseMovementMode::GetMoverComponent() const
{
	return CastChecked<UMoverComponent>(GetOuter());
}

#if WITH_EDITOR
EDataValidationResult UBaseMovementMode::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = EDataValidationResult::Valid;
	for (UBaseMovementModeTransition* Transition : Transitions)
	{
		if (!IsValid(Transition))
		{
			Context.AddError(FText::Format(LOCTEXT("InvalidTransitionOnModeError", "Invalid or missing transition object on mode of type {0}. Clean up the Transitions array."),
				FText::FromString(GetClass()->GetName())));

			Result = EDataValidationResult::Invalid;
		}
		else if (Transition->IsDataValid(Context) == EDataValidationResult::Invalid)
		{
			Result = EDataValidationResult::Invalid;
		}
	}

	return Result;
}
#endif // WITH_EDITOR


bool UBaseMovementMode::HasGameplayTag(FGameplayTag TagToFind, bool bExactMatch) const
{
	if (bExactMatch)
	{
		return GameplayTags.HasTagExact(TagToFind);
	}

	return GameplayTags.HasTag(TagToFind);
}

void UBaseMovementMode::OnRegistered(const FName ModeName)
{
}

void UBaseMovementMode::OnUnregistered()
{
}

void UBaseMovementMode::OnGenerateMove(const FMoverTickStartData& StartState, const FMoverTimeStep& TimeStep, FProposedMove& OutProposedMove) const
{
}

void UBaseMovementMode::OnSimulationTick(const FSimulationTickParams& Params, FMoverTickEndData& OutputState)
{
}

void UBaseMovementMode::OnActivate()
{
}

void UBaseMovementMode::OnDeactivate()
{
}

#undef LOCTEXT_NAMESPACE
