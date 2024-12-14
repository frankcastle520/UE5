// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntityTypes.h"
#include "MassEntityManager.h"
#include "Misc/MTAccessDetector.h"
#include "MassEntityUtils.h"
#include "MassCommands.h"

//@TODO: Consider debug information in case there is an assert when replaying the command buffer
// (e.g., which system added the command, or even file/line number in development builds for the specific call via a macro)

#define COMMAND_PUSHING_CHECK() \
checkf(IsFlushing() == false, TEXT("Trying to push commands is not supported while the given buffer is being flushed")); \
checkf(OwnerThreadId == FPlatformTLS::GetCurrentThreadId(), TEXT("Commands can be pushed only in the same thread where the command buffer was created."))

struct MASSENTITY_API FMassCommandBuffer
{
public:
	FMassCommandBuffer();
	~FMassCommandBuffer();

	/** Adds a new entry to a given TCommand batch command instance */
	template< template<typename... TArgs> typename TCommand, typename... TArgs >
	void PushCommand(const FMassEntityHandle Entity, TArgs&&... InArgs)
	{
		COMMAND_PUSHING_CHECK();

		LLM_SCOPE_BYNAME(TEXT("Mass/PushCommand"));
		TCommand<TArgs...>& Instance = CreateOrAddCommand<TCommand<TArgs...>>();
		Instance.Add(Entity, Forward<TArgs>(InArgs)...);
		++ActiveCommandsCounter;
	}

	template<typename TCommand, typename... TArgs>
	void PushCommand(TArgs&&... InArgs)
	{
		COMMAND_PUSHING_CHECK();

		LLM_SCOPE_BYNAME(TEXT("Mass/PushCommand"));
		TCommand& Instance = CreateOrAddCommand<TCommand>();
		Instance.Add(Forward<TArgs>(InArgs)...);
		++ActiveCommandsCounter;
	}

	/** Adds a new entry to a given TCommand batch command instance */
	template< typename TCommand>
	void PushCommand(const FMassEntityHandle Entity)
	{
		COMMAND_PUSHING_CHECK();

		LLM_SCOPE_BYNAME(TEXT("Mass/PushCommand"));
		CreateOrAddCommand<TCommand>().Add(Entity);
		++ActiveCommandsCounter;
	}

	template< typename TCommand>
	void PushCommand(TConstArrayView<FMassEntityHandle> Entities)
	{
		COMMAND_PUSHING_CHECK();

		LLM_SCOPE_BYNAME(TEXT("Mass/PushCommand"));
		CreateOrAddCommand<TCommand>().Add(Entities);
		++ActiveCommandsCounter;
	}

	template<typename T>
	void AddFragment(FMassEntityHandle Entity)
	{
		static_assert(TIsDerivedFrom<T, FMassFragment>::IsDerived, "Given struct type is not a valid fragment type.");
		PushCommand<FMassCommandAddFragmentsInternal<EMassCommandCheckTime::CompileTimeCheck, T>>(Entity);
	}

	template<typename T>
	void AddFragment_RuntimeCheck(FMassEntityHandle Entity)
	{
		checkf(T::StaticStruct()->IsChildOf(FMassFragment::StaticStruct()), TEXT("Given struct type is not a valid fragment type."));
		PushCommand<FMassCommandAddFragmentsInternal<EMassCommandCheckTime::RuntimeCheck, T>>(Entity);
	}

	template<typename T>
	void RemoveFragment(FMassEntityHandle Entity)
	{
		static_assert(TIsDerivedFrom<T, FMassFragment>::IsDerived, "Given struct type is not a valid fragment type.");
		PushCommand<FMassCommandRemoveFragmentsInternal<EMassCommandCheckTime::CompileTimeCheck, T>>(Entity);
	}

	template<typename T>
	void RemoveFragment_RuntimeCheck(FMassEntityHandle Entity)
	{
		checkf(T::StaticStruct()->IsChildOf(FMassFragment::StaticStruct()), TEXT("Given struct type is not a valid fragment type."));
		PushCommand<FMassCommandRemoveFragmentsInternal<EMassCommandCheckTime::RuntimeCheck, T>>(Entity);
	}

	/** the convenience function equivalent to calling PushCommand<FMassCommandAddTag<T>>(Entity) */
	template<typename T>
	void AddTag(FMassEntityHandle Entity)
	{
		static_assert(TIsDerivedFrom<T, FMassTag>::IsDerived, "Given struct type is not a valid tag type.");
		PushCommand<FMassCommandAddTagsInternal<EMassCommandCheckTime::CompileTimeCheck, T>>(Entity);
	}

	template<typename T>
	void AddTag_RuntimeCheck(FMassEntityHandle Entity)
	{
		checkf(T::StaticStruct()->IsChildOf(FMassTag::StaticStruct()), TEXT("Given struct type is not a valid tag type."));
		PushCommand<FMassCommandAddTagsInternal<EMassCommandCheckTime::RuntimeCheck, T>>(Entity);
	}

	/** the convenience function equivalent to calling PushCommand<FMassCommandRemoveTag<T>>(Entity) */
	template<typename T>
	void RemoveTag(FMassEntityHandle Entity)
	{
		static_assert(TIsDerivedFrom<T, FMassTag>::IsDerived, "Given struct type is not a valid tag type.");
		PushCommand<FMassCommandRemoveTagsInternal<EMassCommandCheckTime::CompileTimeCheck, T>>(Entity);
	}

	template<typename T>
	void RemoveTag_RuntimeCheck(FMassEntityHandle Entity)
	{
		checkf(T::StaticStruct()->IsChildOf(FMassTag::StaticStruct()), TEXT("Given struct type is not a valid tag type."));
		PushCommand<FMassCommandRemoveTagsInternal<EMassCommandCheckTime::RuntimeCheck, T>>(Entity);
	}

	/** the convenience function equivalent to calling PushCommand<FMassCommandSwapTags<TOld, TNew>>(Entity)  */
	template<typename TOld, typename TNew>
	void SwapTags(FMassEntityHandle Entity)
	{
		static_assert(TIsDerivedFrom<TOld, FMassTag>::IsDerived, "Given struct type is not a valid tag type.");
		static_assert(TIsDerivedFrom<TNew, FMassTag>::IsDerived, "Given struct type is not a valid tag type.");
		PushCommand<FMassCommandSwapTagsInternal<EMassCommandCheckTime::CompileTimeCheck, TOld, TNew>>(Entity);
	}

	template<typename TOld, typename TNew>
	void SwapTags_RuntimeCheck(FMassEntityHandle Entity)
	{
		checkf(TOld::StaticStruct()->IsChildOf(FMassTag::StaticStruct()), TEXT("Given struct type is not a valid tag type."));
		checkf(TNew::StaticStruct()->IsChildOf(FMassTag::StaticStruct()), TEXT("Given struct type is not a valid tag type."));
		PushCommand<FMassCommandSwapTagsInternal<EMassCommandCheckTime::RuntimeCheck, TOld, TNew>>(Entity);
	}

	void DestroyEntity(FMassEntityHandle Entity)
	{
		PushCommand<FMassCommandDestroyEntities>(Entity);
	}

	void DestroyEntities(TConstArrayView<FMassEntityHandle> InEntitiesToDestroy)
	{
		PushCommand<FMassCommandDestroyEntities>(InEntitiesToDestroy);
	}

	SIZE_T GetAllocatedSize() const;

	/** 
	 * Appends the commands from the passed buffer into this one
	 * @param InOutOther the source buffer to copy the commands from. Note that after the call the InOutOther will be 
	 *	emptied due to the function using Move semantics
	 */
	void MoveAppend(FMassCommandBuffer& InOutOther);

	bool HasPendingCommands() const 
	{
		return ActiveCommandsCounter > 0;
	}
	bool IsFlushing() const { return bIsFlushing; }

private:
	friend FMassEntityManager;

	void ForceUpdateCurrentThreadID();

	template<typename T>
	T& CreateOrAddCommand()
	{
		const int32 Index = FMassBatchedCommand::GetCommandIndex<T>();

		if (CommandInstances.IsValidIndex(Index) == false)
		{
			CommandInstances.AddZeroed(Index - CommandInstances.Num() + 1);
		}
		else if (CommandInstances[Index])
		{
			return (T&)(*CommandInstances[Index].Get());
		}

		CommandInstances[Index] = MakeUnique<T>();
		return (T&)(*CommandInstances[Index].Get());
	}

	/** 
	 * Executes all accumulated commands. 
	 * @return whether any commands have actually been executed
	 */
	bool Flush(FMassEntityManager& EntityManager);
	void CleanUp();

	FCriticalSection AppendingCommandsCS;

	UE_MT_DECLARE_RW_ACCESS_DETECTOR(PendingBatchCommandsDetector);
	/** 
	 * Commands created for this specific command buffer. All commands in the array are unique (by type) and reusable 
	 * with subsequent PushCommand calls
	 */
	TArray<TUniquePtr<FMassBatchedCommand>> CommandInstances;
	/** 
	 * Commands appended to this command buffer (via FMassCommandBuffer::MoveAppend). These commands are just naive list
	 * of commands, potentially containing duplicates with multiple MoveAppend calls. Once appended these commands are 
	 * not being reused and consumed, destructively, during flushing
	 */
	TArray<TUniquePtr<FMassBatchedCommand>> AppendedCommandInstances;

	int32 ActiveCommandsCounter = 0;

	/** Indicates that this specific MassCommandBuffer is currently flushing its contents */
	bool bIsFlushing = false;

	/** 
	 * Identifies the thread where given FMassCommandBuffer instance was created. Adding commands from other
	 * threads is not supported and we use this value to check that.
	 * Note that it could be const since we set it in the constructor, but we need to recache on server forking.
	 */
	uint32 OwnerThreadId;
};

#undef COMMAND_PUSHING_CHECK
