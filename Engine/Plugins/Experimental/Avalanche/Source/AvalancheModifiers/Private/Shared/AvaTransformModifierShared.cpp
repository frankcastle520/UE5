// Copyright Epic Games, Inc. All Rights Reserved.

#include "Shared/AvaTransformModifierShared.h"

#include "Modifiers/AvaBaseModifier.h"
#include "GameFramework/Actor.h"

void FAvaTransformSharedModifierState::Save(const AActor* InActor, EAvaTransformSharedModifierState InSaveState)
{
	if (const UAvaBaseModifier* Modifier = ModifierWeak.Get())
	{
		if (InActor)
		{
			if (EnumHasAnyFlags(InSaveState, EAvaTransformSharedModifierState::Location)
				&& !EnumHasAnyFlags(SaveState, EAvaTransformSharedModifierState::Location))
			{
				ActorTransform.SetLocation(InActor->GetActorLocation());
				EnumAddFlags(SaveState, EAvaTransformSharedModifierState::Location);
			}

			if (EnumHasAnyFlags(InSaveState, EAvaTransformSharedModifierState::Rotation)
				&& !EnumHasAnyFlags(SaveState, EAvaTransformSharedModifierState::Rotation))
			{
				ActorTransform.SetRotation(InActor->GetActorRotation().Quaternion());
				EnumAddFlags(SaveState, EAvaTransformSharedModifierState::Rotation);
			}

			if (EnumHasAnyFlags(InSaveState, EAvaTransformSharedModifierState::Scale)
				&& !EnumHasAnyFlags(SaveState, EAvaTransformSharedModifierState::Scale))
			{
				ActorTransform.SetScale3D(InActor->GetActorScale3D());
				EnumAddFlags(SaveState, EAvaTransformSharedModifierState::Scale);
			}
		}
	}
}

void FAvaTransformSharedModifierState::Restore(AActor* InActor, EAvaTransformSharedModifierState InRestoreState)
{
	if (const UAvaBaseModifier* Modifier = ModifierWeak.Get())
	{
		if (InActor)
		{
			FTransform RestoreTransform = ActorTransform;
			const FTransform& CurrentActorTransform = InActor->GetActorTransform();

			if (!EnumHasAnyFlags(InRestoreState, EAvaTransformSharedModifierState::Location)
				|| !EnumHasAnyFlags(SaveState, EAvaTransformSharedModifierState::Location))
			{
				RestoreTransform.SetLocation(CurrentActorTransform.GetLocation());
			}

			if (!EnumHasAnyFlags(InRestoreState, EAvaTransformSharedModifierState::Rotation)
				|| !EnumHasAnyFlags(SaveState, EAvaTransformSharedModifierState::Rotation))
			{
				RestoreTransform.SetRotation(CurrentActorTransform.GetRotation());
			}

			if (!EnumHasAnyFlags(InRestoreState, EAvaTransformSharedModifierState::Scale)
				|| !EnumHasAnyFlags(SaveState, EAvaTransformSharedModifierState::Scale))
			{
				RestoreTransform.SetScale3D(CurrentActorTransform.GetScale3D());
			}

			if (!CurrentActorTransform.Equals(RestoreTransform))
			{
				InActor->SetActorTransform(RestoreTransform);
			}

			EnumRemoveFlags(SaveState, InRestoreState);
		}
	}
}

void FAvaTransformSharedActorState::Save(EAvaTransformSharedModifierState InSaveState)
{
	if (const AActor* Actor = ActorWeak.Get())
	{
		if (EnumHasAnyFlags(InSaveState, EAvaTransformSharedModifierState::Location)
			&& !EnumHasAnyFlags(SaveState, EAvaTransformSharedModifierState::Location))
		{
			ActorTransform.SetLocation(Actor->GetActorLocation());
			EnumAddFlags(SaveState, EAvaTransformSharedModifierState::Location);
		}

		if (EnumHasAnyFlags(InSaveState, EAvaTransformSharedModifierState::Rotation)
			&& !EnumHasAnyFlags(SaveState, EAvaTransformSharedModifierState::Rotation))
		{
			ActorTransform.SetRotation(Actor->GetActorRotation().Quaternion());
			EnumAddFlags(SaveState, EAvaTransformSharedModifierState::Rotation);
		}

		if (EnumHasAnyFlags(InSaveState, EAvaTransformSharedModifierState::Scale)
			&& !EnumHasAnyFlags(SaveState, EAvaTransformSharedModifierState::Scale))
		{
			ActorTransform.SetScale3D(Actor->GetActorScale3D());
			EnumAddFlags(SaveState, EAvaTransformSharedModifierState::Scale);
		}
	}
}

void FAvaTransformSharedActorState::Restore(EAvaTransformSharedModifierState InRestoreState)
{
	if (AActor* Actor = ActorWeak.Get())
	{
		FTransform RestoreTransform = ActorTransform;
		const FTransform& CurrentActorTransform = Actor->GetActorTransform();

		if (!EnumHasAnyFlags(InRestoreState, EAvaTransformSharedModifierState::Location)
			|| !EnumHasAnyFlags(SaveState, EAvaTransformSharedModifierState::Location))
		{
			RestoreTransform.SetLocation(CurrentActorTransform.GetLocation());
		}

		if (!EnumHasAnyFlags(InRestoreState, EAvaTransformSharedModifierState::Rotation)
			|| !EnumHasAnyFlags(SaveState, EAvaTransformSharedModifierState::Rotation))
		{
			RestoreTransform.SetRotation(CurrentActorTransform.GetRotation());
		}

		if (!EnumHasAnyFlags(InRestoreState, EAvaTransformSharedModifierState::Scale)
			|| !EnumHasAnyFlags(SaveState, EAvaTransformSharedModifierState::Scale))
		{
			RestoreTransform.SetScale3D(CurrentActorTransform.GetScale3D());
		}

		if (!CurrentActorTransform.Equals(RestoreTransform))
		{
			Actor->SetActorTransform(RestoreTransform);
		}

		EnumRemoveFlags(SaveState, InRestoreState);
	}
}

void UAvaTransformModifierShared::SaveActorState(UAvaBaseModifier* InModifierContext, AActor* InActor, EAvaTransformSharedModifierState InSaveState)
{
	if (!IsValid(InActor))
	{
		return;
	}

	FAvaTransformSharedActorState& ActorState = ActorStates.FindOrAdd(FAvaTransformSharedActorState(InActor));
	ActorState.Save(InSaveState);

	FAvaTransformSharedModifierState& ModifierState = ActorState.ModifierStates.FindOrAdd(FAvaTransformSharedModifierState(InModifierContext));
	ModifierState.Save(InActor, InSaveState);
}

void UAvaTransformModifierShared::RestoreActorState(UAvaBaseModifier* InModifierContext, AActor* InActor, EAvaTransformSharedModifierState InRestoreState)
{
	if (!IsValid(InActor))
	{
		return;
	}

	FAvaTransformSharedActorState* ActorState = ActorStates.Find(FAvaTransformSharedActorState(InActor));
	if (!ActorState)
	{
		return;
	}

	FAvaTransformSharedModifierState* ActorModifierState = ActorState->ModifierStates.Find(FAvaTransformSharedModifierState(InModifierContext));
	if (!ActorModifierState)
	{
		return;
	}

	// restore modifier state and remove it
	ActorModifierState->Restore(InActor, InRestoreState);

	if (ActorModifierState->SaveState == EAvaTransformSharedModifierState::None)
	{
		ActorState->ModifierStates.Remove(*ActorModifierState);
	}

	// Restore original actor state and remove it
	if (ActorState->ModifierStates.IsEmpty())
	{
		ActorState->Restore(EAvaTransformSharedModifierState::All);
		ActorStates.Remove(*ActorState);
	}
}

FAvaTransformSharedActorState* UAvaTransformModifierShared::FindActorState(AActor* InActor)
{
	if (!IsValid(InActor))
	{
		return nullptr;
	}

	return ActorStates.Find(FAvaTransformSharedActorState(InActor));
}

TSet<FAvaTransformSharedActorState*> UAvaTransformModifierShared::FindActorsState(UAvaBaseModifier* InModifierContext)
{
	TSet<FAvaTransformSharedActorState*> ModifierActorStates;

	for (FAvaTransformSharedActorState& ActorState : ActorStates)
	{
		if (ActorState.ModifierStates.Contains(FAvaTransformSharedModifierState(InModifierContext)))
		{
			ModifierActorStates.Add(&ActorState);
		}
	}

	return ModifierActorStates;
}

void UAvaTransformModifierShared::RestoreActorsState(UAvaBaseModifier* InModifierContext, const TSet<AActor*>* InActors, EAvaTransformSharedModifierState InRestoreState)
{
	const FAvaTransformSharedModifierState SearchModifierState(InModifierContext);
	TSet<AActor*> LinkedModifierActors;
	TSet<UActorModifierCoreBase*> LinkedActorModifiers;

	for (const FAvaTransformSharedActorState& ActorState : ActorStates)
	{
		AActor* Actor = ActorState.ActorWeak.Get();
		if (!Actor)
		{
			continue;
		}

		if (!ActorState.ModifierStates.Contains(SearchModifierState))
		{
			continue;
		}

		if (InActors && !InActors->Contains(Actor))
		{
			continue;
		}

		// Collect actors affected by modifier
		LinkedModifierActors.Add(Actor);

		// Collect linked actor modifiers
		for (const FAvaTransformSharedModifierState& ModifierState : ActorState.ModifierStates)
		{
			if (UActorModifierCoreBase* Modifier = ModifierState.ModifierWeak.Get())
			{
				LinkedActorModifiers.Add(Modifier);
			}
		}
	}

	// Locking state to prevent from updating when restoring state
	// When destroyed : Unlocking state of modifier
	FActorModifierCoreScopedLock ModifiersLock(LinkedActorModifiers);

	// Restore actor state
	for (AActor* Actor : LinkedModifierActors)
	{
		RestoreActorState(InModifierContext, Actor, InRestoreState);
	}
}

void UAvaTransformModifierShared::RestoreActorsState(UAvaBaseModifier* InModifierContext, const TSet<TWeakObjectPtr<AActor>>& InActors, EAvaTransformSharedModifierState InRestoreState)
{
	TSet<AActor*> Actors;
	Algo::Transform(InActors, Actors, [](const TWeakObjectPtr<AActor>& InActor)->AActor*{ return InActor.Get(); });

	RestoreActorsState(InModifierContext, &Actors, InRestoreState);
}

bool UAvaTransformModifierShared::IsActorStateSaved(UAvaBaseModifier* InModifierContext, AActor* InActor)
{
	if (const FAvaTransformSharedActorState* ActorState = FindActorState(InActor))
	{
		return ActorState->ModifierStates.Contains(FAvaTransformSharedModifierState(InModifierContext));
	}

	return false;
}

bool UAvaTransformModifierShared::IsActorsStateSaved(UAvaBaseModifier* InModifierContext)
{
	const FAvaTransformSharedModifierState ModifierState(InModifierContext);

	for (const FAvaTransformSharedActorState& ActorState : ActorStates)
	{
		if (ActorState.ModifierStates.Contains(ModifierState))
		{
			return true;
		}
	}

	return false;
}

void UAvaTransformModifierShared::PostLoad()
{
	Super::PostLoad();

	// Remove invalid items when loading
	for (FAvaTransformSharedActorState& ActorState : ActorStates)
	{
		ActorState.ModifierStates.Remove(FAvaTransformSharedModifierState(nullptr));
	}
}
