// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modifiers/Blueprints/ActorModifierCoreBlueprintBase.h"

#define LOCTEXT_NAMESPACE "ActorModifierCoreBlueprintBase"

void UActorModifierCoreBlueprintBase::FlagModifierDirty()
{
	if (!IsTemplate())
	{
		LogModifier(TEXT("Blueprint modifier flagged dirty"));

		MarkModifierDirty(/** Execute */true);
	}
}

#if WITH_EDITOR
void UActorModifierCoreBlueprintBase::PostEditChangeProperty(FPropertyChangedEvent& InPropertyChangedEvent)
{
	Super::PostEditChangeProperty(InPropertyChangedEvent);

	// Only trigger modifier if not interactive and property owned by children of this class
	if (InPropertyChangedEvent.Property
		&& InPropertyChangedEvent.Property->GetOwnerClass()
		&& InPropertyChangedEvent.Property->GetOwnerClass()->IsChildOf<UActorModifierCoreBlueprintBase>()
		&& InPropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		FlagModifierDirty();
	}
}
#endif

void UActorModifierCoreBlueprintBase::OnModifierCDOSetup(FActorModifierCoreMetadata& InMetadata)
{
	Super::OnModifierCDOSetup(InMetadata);

	LogModifier(TEXT("Blueprint modifier setup"));

	// Since the blueprint utility functions returns a copy of the struct, make sure we get a result
	OnModifierSetupEvent(InMetadata, /** OutMetadata */InMetadata);

	if (InMetadata.GetName().IsNone())
	{
		LogModifier(TEXT("Blueprint modifier setup failed : Name was not defined"), true);
	}
}

void UActorModifierCoreBlueprintBase::OnModifierAdded(EActorModifierCoreEnableReason InReason)
{
	Super::OnModifierAdded(InReason);

	AActor* TargetActor = GetModifiedActor();

	if (IsValid(TargetActor))
	{
		LogModifier(FString::Printf(TEXT("Blueprint modifier added with reason %i"), InReason));

		OnModifierAddedEvent(TargetActor, InReason);
	}
}

void UActorModifierCoreBlueprintBase::OnModifierEnabled(EActorModifierCoreEnableReason InReason)
{
	Super::OnModifierEnabled(InReason);

	AActor* TargetActor = GetModifiedActor();

	if (IsValid(TargetActor))
	{
		LogModifier(FString::Printf(TEXT("Blueprint modifier enabled with reason %i"), InReason));

		OnModifierEnabledEvent(TargetActor, InReason);
	}
}

void UActorModifierCoreBlueprintBase::OnModifierDisabled(EActorModifierCoreDisableReason InReason)
{
	Super::OnModifierDisabled(InReason);

	AActor* TargetActor = GetModifiedActor();

	if (IsValid(TargetActor))
	{
		LogModifier(FString::Printf(TEXT("Blueprint modifier disabled with reason %i"), InReason));

		OnModifierDisabledEvent(TargetActor, InReason);
	}
}

void UActorModifierCoreBlueprintBase::OnModifierRemoved(EActorModifierCoreDisableReason InReason)
{
	Super::OnModifierRemoved(InReason);

	AActor* TargetActor = GetModifiedActor();

	if (IsValid(TargetActor))
	{
		LogModifier(FString::Printf(TEXT("Blueprint modifier removed with reason %i"), InReason));

		OnModifierRemovedEvent(TargetActor, InReason);
	}
}

void UActorModifierCoreBlueprintBase::SavePreState()
{
	Super::SavePreState();

	AActor* TargetActor = GetModifiedActor();

	if (IsValid(TargetActor))
	{
		LogModifier(TEXT("Blueprint modifier save pre state"));

		OnModifierSaveStateEvent(TargetActor);
	}
}

void UActorModifierCoreBlueprintBase::RestorePreState()
{
	Super::RestorePreState();

	AActor* TargetActor = GetModifiedActor();

	if (IsValid(TargetActor))
	{
		LogModifier(TEXT("Blueprint modifier restore pre state"));

		OnModifierRestoreStateEvent(TargetActor);
	}
}

void UActorModifierCoreBlueprintBase::Apply()
{
	FText FailReason = FText::GetEmpty();
	AActor* TargetActor = GetModifiedActor();

	LogModifier(TEXT("Blueprint modifier apply"));

	if (IsValid(TargetActor) && OnModifierApplyEvent(TargetActor, FailReason))
	{
		Next();
	}
	else
	{
		// Fail reason must be set
		if (FailReason.IsEmpty())
		{
			FailReason = FText::Format(LOCTEXT("ApplyFailed", "{0} : Blueprint modifier {1} apply failed"), FText::FromString(TargetActor ? TargetActor->GetActorNameOrLabel() : TEXT("?")), FText::FromName(GetModifierName()));
		}

		Fail(FailReason);
	}
}

#undef LOCTEXT_NAMESPACE
