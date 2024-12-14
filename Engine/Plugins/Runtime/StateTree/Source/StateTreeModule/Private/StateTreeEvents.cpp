// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeEvents.h"
#include "StateTreeTypes.h"
#include "VisualLogger/VisualLogger.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(StateTreeEvents)

//----------------------------------------------------------------//
// FStateTreeSharedEvent
//----------------------------------------------------------------//

void FStateTreeSharedEvent::AddStructReferencedObjects(FReferenceCollector& Collector)
{
	if (IsValid())
	{
		Collector.AddPropertyReferencesWithStructARO(FStateTreeEvent::StaticStruct(), Event.Get());
	}
}


//----------------------------------------------------------------//
// FStateTreeEventQueue
//----------------------------------------------------------------//

void FStateTreeEventQueue::SendEvent(const UObject* Owner, const FGameplayTag& Tag, const FConstStructView Payload, const FName Origin)
{
	if (!Tag.IsValid() && !Payload.IsValid())
	{
		UE_VLOG_UELOG(Owner, LogStateTree, Error, TEXT("%s: An event with an invalid tag and payload has been sent to '%s'. This is not allowed."), ANSI_TO_TCHAR(__FUNCTION__), *GetNameSafe(Owner));
		return;
	}

	if (SharedEvents.Num() >= MaxActiveEvents)
	{
		UE_VLOG_UELOG(Owner, LogStateTree, Error, TEXT("%s: Too many events send on '%s'. Dropping event %s"), ANSI_TO_TCHAR(__FUNCTION__), *GetNameSafe(Owner), *Tag.ToString());
		return;
	}

	SharedEvents.Emplace(Tag, Payload, Origin);
}

void FStateTreeEventQueue::ConsumeEvent(const FStateTreeSharedEvent& Event)
{
	SharedEvents.RemoveAll([&EventToRemove = Event](const FStateTreeSharedEvent& Event)
	{
		return Event == EventToRemove;
	});
}
