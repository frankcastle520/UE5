// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayTagContainer.h"
#include "StructUtils/StructView.h"
#include "StateTreeIndexTypes.h"
#include "StateTreeEvents.generated.h"

/** Enum used for flow control during event iteration. */
UENUM()
enum class EStateTreeLoopEvents : uint8
{
	/** Continues to next event. */
	Next,
	/** Stops the event handling loop. */
	Break,
	/** Consumes and removes the current event. */
	Consume,
};

/**
 * StateTree event with payload.
 */
USTRUCT(BlueprintType)
struct STATETREEMODULE_API FStateTreeEvent
{
	GENERATED_BODY()

	FStateTreeEvent() = default;

	explicit FStateTreeEvent(const FGameplayTag InTag)
		: Tag(InTag)
	{
	}
	
	explicit FStateTreeEvent(const FGameplayTag InTag, const FConstStructView InPayload, const FName InOrigin)
		: Tag(InTag)
		, Payload(InPayload)
		, Origin(InOrigin)
	{
	}

	friend FORCEINLINE uint32 GetTypeHash(const FStateTreeEvent& Event)
	{
		uint32 Hash = GetTypeHash(Event.Tag);
		
		if (Event.Payload.IsValid())
		{
			Hash = HashCombineFast(Hash, Event.Payload.GetScriptStruct()->GetStructTypeHash(Event.Payload.GetMemory()));
		}

		return Hash;
	}
	
	/** Tag describing the event */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Default", meta=(Categories="StateTreeEvent"))
	FGameplayTag Tag;

	/** Optional payload for the event. */ 
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Default")
	FInstancedStruct Payload;

	/** Optional info to describe who sent the event. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Default")
	FName Origin;
};

/**
 * A struct wrapping FStateTreeEvent in shared struct, used to make it easier to refer to the events during State Tree update.
 */
USTRUCT()
struct FStateTreeSharedEvent
{
	GENERATED_BODY()

	FStateTreeSharedEvent() = default;

	explicit FStateTreeSharedEvent(const FGameplayTag InTag, const FConstStructView InPayload, const FName InOrigin)
		: Event(MakeShared<FStateTreeEvent>(InTag, InPayload, InOrigin))
	{}

	explicit FStateTreeSharedEvent(const FStateTreeEvent& InEvent)
		: Event(MakeShared<FStateTreeEvent>(InEvent))
	{}

	void AddStructReferencedObjects(FReferenceCollector& Collector);

	const FStateTreeEvent* Get() const
	{
		return Event.Get();
	}

	FStateTreeEvent* GetMutable()
	{
		return Event.Get();
	}

	const FStateTreeEvent* operator->() const
	{
		return Event.Get();
	}

	FStateTreeEvent* operator->()
	{
		return Event.Get();
	}

	const FStateTreeEvent& operator*()
	{
		check(Event.IsValid());
		return *Event.Get();
	}

	FStateTreeEvent& operator*() const
	{
		check(Event.IsValid());
		return *Event.Get();
	}

	bool IsValid() const
	{
		return Event.IsValid();
	}

	bool operator==(const FStateTreeSharedEvent& Other) const
	{
		return Event == Other.Event;
	}

protected:
	TSharedPtr<FStateTreeEvent> Event;
};

template<>
struct TStructOpsTypeTraits<FStateTreeSharedEvent> : public TStructOpsTypeTraitsBase2<FStateTreeSharedEvent>
{
	enum
	{
		WithAddStructReferencedObjects = true,
	};
};

/**
 * Event queue buffering all the events to be processed by a State Tree.
 */
USTRUCT()
struct STATETREEMODULE_API FStateTreeEventQueue
{
	GENERATED_BODY()

	/** Maximum number of events that can be buffered. */
	static constexpr int32 MaxActiveEvents = 64;

	/** @return const view to all the events in the buffer. */
	TConstArrayView<FStateTreeSharedEvent> GetEventsView() const
	{
		return SharedEvents;
	}

	/** @return view to all the events in the buffer. */
	TArrayView<FStateTreeSharedEvent> GetMutableEventsView()
	{
		return SharedEvents;
	}

	/** Resets the events in the event queue */
	void Reset()
	{
		SharedEvents.Reset();
	}

	/** @return true if the queue has any events. */
	bool HasEvents() const
	{
		return !SharedEvents.IsEmpty();
	}
	
	/**
	 * Buffers and event to be sent to the State Tree.
	 * @param Owner Optional pointer to an owner UObject that is used for logging errors.
	 * @param Tag tag identifying the event.
	 * @param Payload Optional reference to the payload struct.
	 * @param Origin Optional name identifying the origin of the event.
	 */
	void SendEvent(const UObject* Owner, const FGameplayTag& Tag, const FConstStructView Payload = FConstStructView(), const FName Origin = FName());

	/** Consumes and removes the specified event from the event queue. */
	void ConsumeEvent(const FStateTreeSharedEvent& Event);

	/**
	 * Iterates over all events.
	 * @param Function a lambda which takes const FStateTreeSharedEvent& Event, and returns EStateTreeLoopEvents.
	 */
	template<typename TFunc>
	void ForEachEvent(TFunc&& Function) const
	{
		for (TArray<FStateTreeSharedEvent>::TConstIterator It(SharedEvents); It; ++It)
		{
			const EStateTreeLoopEvents Result = Function(*It);
			if (Result == EStateTreeLoopEvents::Break)
			{
				break;
			}
			if (Result == EStateTreeLoopEvents::Consume)
			{
				It.RemoveCurrent();
			}
		}
	}

	UE_DEPRECATED(5.5, "Use GetEventsView() instead.")
	TConstArrayView<FStateTreeEvent> GetEvents() const { return {};	}

protected:
	// Used by FStateTreeExecutionState to implement deprecated functionality.
	TArray<FStateTreeSharedEvent>& GetEventsArray() { return SharedEvents; };

	UPROPERTY()
	TArray<FStateTreeSharedEvent> SharedEvents;

	friend struct FStateTreeInstanceData;
};
