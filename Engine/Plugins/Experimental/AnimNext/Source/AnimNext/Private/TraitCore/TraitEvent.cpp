// Copyright Epic Games, Inc. All Rights Reserved.

#include "TraitCore/TraitEvent.h"
#include "TraitCore/TraitEventList.h"
#include "TraitCore/TraitBinding.h"
#include "TraitCore/TraitStackBinding.h"
#include "Graph/GraphInstanceComponent.h"

bool FAnimNextTraitEvent::DecrementLifetime(UE::AnimNext::FTraitEventList& OutputEventList)
{
	const bool bExpired = Lifetime.Decrement();
	if (bExpired)
	{
		OnExpired(OutputEventList);
	}

	return bExpired;
}

namespace UE::AnimNext
{
	void RaiseTraitEvent(FExecutionContext& Context, const FTraitStackBinding& StackBinding, FAnimNextTraitEvent& Event)
	{
		if (!StackBinding.IsValid() || !Event.IsValid())
		{
			return;
		}

		FTraitBinding Binding;
		ensure(StackBinding.GetTopTrait(Binding));
		do
		{
			const ETraitStackPropagation Propagation = Binding.GetTrait()->OnTraitEvent(Context, Binding, Event);
			if (Propagation == ETraitStackPropagation::Stop || Event.IsConsumed())
			{
				// We don't wish to propagate the call further, stop here
				break;
			}
		} while (StackBinding.GetParentTrait(Binding, Binding));
	}

	void RaiseTraitEvents(FExecutionContext& Context, const FTraitStackBinding& StackBinding, const FTraitEventList& EventList)
	{
		if (!StackBinding.IsValid())
		{
			return;
		}

		// Event handlers can raise events and as such the list may change while we iterate
		// However, if an event is added while we iterate, we will not visit it
		const int32 NumEvents = EventList.Num();
		for (int32 EventIndex = 0; EventIndex < NumEvents; ++EventIndex)
		{
			const FAnimNextTraitEventPtr Event = EventList[EventIndex];
			RaiseTraitEvent(Context, StackBinding, *Event);
		}
	}

	void RaiseTraitEvents(FExecutionContext& Context, FGraphInstanceComponent& Component, const FTraitEventList& EventList)
	{
		// Event handlers can raise events and as such the list may change while we iterate
		// However, if an event is added while we iterate, we will not visit it
		const int32 NumEvents = EventList.Num();
		for (int32 EventIndex = 0; EventIndex < NumEvents; ++EventIndex)
		{
			const FAnimNextTraitEventPtr Event = EventList[EventIndex];
			if (Event->IsValid())
			{
				Component.OnTraitEvent(Context, *Event);
			}
		}
	}
}
