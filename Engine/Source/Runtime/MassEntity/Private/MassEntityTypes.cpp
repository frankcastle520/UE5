// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassEntityTypes.h"

#include "MassArchetypeData.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MassEntityTypes)

DEFINE_STAT(STAT_Mass_Total);

DEFINE_TYPEBITSET(FMassFragmentBitSet);
DEFINE_TYPEBITSET(FMassTagBitSet);
DEFINE_TYPEBITSET(FMassChunkFragmentBitSet);
DEFINE_TYPEBITSET(FMassSharedFragmentBitSet);
DEFINE_TYPEBITSET(FMassConstSharedFragmentBitSet);
DEFINE_TYPEBITSET(FMassExternalSubsystemBitSet);

//-----------------------------------------------------------------------------
// FMassArchetypeSharedFragmentValues
//-----------------------------------------------------------------------------
FConstSharedStruct FMassArchetypeSharedFragmentValues::AddConstSharedFragment(const FConstSharedStruct& Fragment)
{
	check(Fragment.IsValid());
	const UScriptStruct* StructType = Fragment.GetScriptStruct();
	if (!ensureMsgf(ContainsType(StructType) == false, TEXT("Shared Fragment of type %s already added to FMassArchetypeSharedFragmentValues"), *GetNameSafe(StructType)))
	{
		FConstSharedStruct ExistingSharedStruct = GetConstSharedFragmentStruct(StructType);
		ensureMsgf(ExistingSharedStruct.IsValid(), TEXT("Shared Fragment of type %s already added as NON-CONST shared struct"), *GetNameSafe(StructType));
		return ExistingSharedStruct;
	}

	check(StructType);
	ConstSharedFragmentBitSet.Add(*StructType);
	FConstSharedStruct& StructInstance = ConstSharedFragments.Add_GetRef(Fragment);
	DirtyHashCache();
	return StructInstance;
}

FSharedStruct FMassArchetypeSharedFragmentValues::AddSharedFragment(const FSharedStruct& Fragment)
{
	check(Fragment.IsValid());
	const UScriptStruct* StructType = Fragment.GetScriptStruct();
	if (!ensureMsgf(ContainsType(StructType) == false, TEXT("Shared Fragment of type %s already added to FMassArchetypeSharedFragmentValues"), *GetNameSafe(StructType)))
	{
		FSharedStruct ExistingSharedStruct = GetSharedFragmentStruct(StructType);
		ensureMsgf(ExistingSharedStruct.IsValid(), TEXT("Shared Fragment of type %s already added as CONST shared struct"), *GetNameSafe(StructType));
		return ExistingSharedStruct;
	}

	check(StructType);
	SharedFragmentBitSet.Add(*StructType);
	FSharedStruct& StructInstance = SharedFragments.Add_GetRef(Fragment);
	DirtyHashCache();
	return StructInstance;
}

void FMassArchetypeSharedFragmentValues::ReplaceSharedFragments(TConstArrayView<FSharedStruct> Fragments)
{
	DirtyHashCache();
	for (const FSharedStruct& NewFragment : Fragments)
	{
		const UScriptStruct* NewFragScriptStruct = NewFragment.GetScriptStruct();
		check(NewFragScriptStruct);

		bool bEntryFound = false;
		for (FSharedStruct& MyFragment : SharedFragments)
		{
			if (MyFragment.GetScriptStruct() == NewFragScriptStruct)
			{
				MyFragment = NewFragment;
				bEntryFound = true;
				break;
			}
		}
		ensureMsgf(bEntryFound, TEXT("Existing fragment of type %s could not be found"), *GetNameSafe(NewFragScriptStruct));
	}
}

uint32 FMassArchetypeSharedFragmentValues::CalculateHash() const
{
	if (!testableEnsureMsgf(bSorted, TEXT("Expecting the containers to be sorted for the hash caluclation to be consistent")))
	{
		return 0;
	}

	// Fragments are not part of the uniqueness 
	uint32 Hash = 0;
	for (const FConstSharedStruct& Fragment : ConstSharedFragments)
	{
		Hash = PointerHash(Fragment.GetMemory(), Hash);
	}

	for (const FSharedStruct& Fragment : SharedFragments)
	{
		Hash = PointerHash(Fragment.GetMemory(), Hash);
	}

	return Hash;
}

namespace UE::Mass::Private
{
	template<typename TSharedStruct>
	int32 CountInvalid(const TArray<TSharedStruct>& View)
	{
		int32 Count = 0;
		for (const TSharedStruct& SharedStruct : View)
		{
			const UScriptStruct* StructType = SharedStruct.GetScriptStruct();
			Count += StructType ? 0 : 1;
		}
		return Count;
	}

	/** Note that this function assumes that both ViewA and ViewB do not contain duplicates */
	template<typename TSharedStruct, bool bSkipNulls=true>
	bool ArraysHaveSameContents(const TArray<TSharedStruct>& ViewA, const TArray<TSharedStruct>& ViewB)
	{
		if constexpr (bSkipNulls)
		{
			const int32 NullstCountA = CountInvalid(ViewA);
			const int32 NullstCountB = CountInvalid(ViewB);
			if (ViewA.Num() - NullstCountA != ViewB.Num() - NullstCountB)
			{
				return false;
			}
		}
		else if (ViewA.Num() != ViewB.Num())
		{
			return false;
		}

		for (const TSharedStruct& SharedStruct : ViewA)
		{
			const UScriptStruct* StructType = SharedStruct.GetScriptStruct();
			if constexpr (bSkipNulls)
			{
				if (StructType == nullptr)
				{
					continue;
				}
			}
			const int32 FragmentIndex = ViewB.IndexOfByPredicate(FStructTypeEqualOperator(StructType));
			if (FragmentIndex == INDEX_NONE)
			{
				return false;
			}
			if (ViewB[FragmentIndex].CompareStructValues(SharedStruct) == false)
			{
				return false;
			}
		}

		return true;
	}
}

bool FMassArchetypeSharedFragmentValues::HasSameValues(const FMassArchetypeSharedFragmentValues& Other) const
{
	if (SharedFragmentBitSet.IsEquivalent(Other.SharedFragmentBitSet) == false
		|| ConstSharedFragmentBitSet.IsEquivalent(Other.ConstSharedFragmentBitSet) == false)
	{
		return false;
	}

	return UE::Mass::Private::ArraysHaveSameContents(SharedFragments, Other.GetSharedFragments())
		&& UE::Mass::Private::ArraysHaveSameContents(ConstSharedFragments, Other.GetConstSharedFragments());
}

int32 FMassArchetypeSharedFragmentValues::Append(const FMassArchetypeSharedFragmentValues& Other)
{
	int32 AddedOrModifiedCount = 0;

	for (const FSharedStruct& SharedStruct : Other.GetSharedFragments())
	{
		const UScriptStruct* StructType = SharedStruct.GetScriptStruct();
		check(StructType);
		if (SharedFragmentBitSet.Contains(*StructType))
		{
			const int32 FragmentIndex = SharedFragments.IndexOfByPredicate(FStructTypeEqualOperator(StructType));
			if (testableEnsureMsgf(FragmentIndex != INDEX_NONE, TEXT("%hs trying to switch const fragment to non-const, type %s")
				, __FUNCTION__, *StructType->GetName()))
			{
				SharedFragments[FragmentIndex] = SharedStruct;
				++AddedOrModifiedCount;
			}
		}
		else
		{
			SharedFragments.Add(SharedStruct);
			++AddedOrModifiedCount;
		}
	}

	for (const FConstSharedStruct& SharedStruct : Other.GetConstSharedFragments())
	{
		const UScriptStruct* StructType = SharedStruct.GetScriptStruct();
		check(StructType);
		if (ConstSharedFragmentBitSet.Contains(*StructType))
		{
			const int32 FragmentIndex = ConstSharedFragments.IndexOfByPredicate(FStructTypeEqualOperator(StructType));
			if (testableEnsureMsgf(FragmentIndex != INDEX_NONE, TEXT("%hs trying to switch non-const fragment to const, type %s")
				, __FUNCTION__, *StructType->GetName()))
			{
				ConstSharedFragments[FragmentIndex] = SharedStruct;
				++AddedOrModifiedCount;
			}
		}
		else
		{
			ConstSharedFragments.Add(SharedStruct);
			++AddedOrModifiedCount;
		}
	}

	SharedFragmentBitSet += Other.SharedFragmentBitSet;
	ConstSharedFragmentBitSet += Other.ConstSharedFragmentBitSet;
	DirtyHashCache();

	return AddedOrModifiedCount;
}

int32 FMassArchetypeSharedFragmentValues::Remove(const FMassSharedFragmentBitSet& SharedFragmentToRemoveBitSet)
{
	int32 RemovedCount = 0;
	FMassSharedFragmentBitSet CommonFragments = (SharedFragmentBitSet & SharedFragmentToRemoveBitSet);
	FMassSharedFragmentBitSet::FIndexIterator It = CommonFragments.GetIndexIterator();
	while(It)
	{
		const UScriptStruct* StructType = CommonFragments.GetTypeAtIndex(*It);
		check(StructType);

		const int32 RegularFragmentIndex = SharedFragments.IndexOfByPredicate(FStructTypeEqualOperator(StructType));
		if (RegularFragmentIndex != INDEX_NONE)
		{
			SharedFragments[RegularFragmentIndex].Reset();
			++RemovedCount;
		}

		++It;
	}

	if (RemovedCount)
	{
		SharedFragments.RemoveAllSwap([](const FSharedStruct& SharedStruct) { return !SharedStruct.IsValid(); });
		SharedFragmentBitSet -= CommonFragments;
		DirtyHashCache();
	}
	return RemovedCount;
}

int32 FMassArchetypeSharedFragmentValues::Remove(const FMassConstSharedFragmentBitSet& ConstSharedFragmentToRemoveBitSet)
{
	int32 RemovedCount = 0;
	FMassConstSharedFragmentBitSet CommonFragments = (ConstSharedFragmentBitSet & ConstSharedFragmentToRemoveBitSet);
	FMassConstSharedFragmentBitSet::FIndexIterator It = CommonFragments.GetIndexIterator();
	while(It)
	{
		const UScriptStruct* StructType = CommonFragments.GetTypeAtIndex(*It);
		check(StructType);

		const int32 RegularFragmentIndex = ConstSharedFragments.IndexOfByPredicate(FStructTypeEqualOperator(StructType));
		if (RegularFragmentIndex != INDEX_NONE)
		{
			ConstSharedFragments[RegularFragmentIndex].Reset();
			++RemovedCount;
		}

		++It;
	}

	if (RemovedCount)
	{
		ConstSharedFragments.RemoveAllSwap([](const FConstSharedStruct& SharedStruct) { return !SharedStruct.IsValid(); });
		ConstSharedFragmentBitSet -= CommonFragments;
		DirtyHashCache();
	}
	return RemovedCount;
}

//-----------------------------------------------------------------------------
// FMassGenericPayloadView
//-----------------------------------------------------------------------------
void FMassGenericPayloadView::SwapElementsToEnd(const int32 StartIndex, int32 NumToMove)
{
	check(StartIndex >= 0 && NumToMove >= 0);

	if (UNLIKELY(NumToMove <= 0 || StartIndex < 0))
	{
		return;
	}

	TArray<uint8, TInlineAllocator<16>> MovedElements;

	for (FStructArrayView& StructArrayView : Content)
	{
		check((StartIndex + NumToMove) <= StructArrayView.Num());
		if (StartIndex + NumToMove >= StructArrayView.Num() - 1)
		{
			// nothing to do here, the elements are already at the back
			continue;
		}

		uint8* ViewData = static_cast<uint8*>(StructArrayView.GetData());
		const uint32 ElementSize = StructArrayView.GetTypeSize();
		const uint32 MovedStartOffset = StartIndex * ElementSize;
		const uint32 MovedSize = NumToMove * ElementSize;
		const uint32 MoveOffset = (StructArrayView.Num() - (StartIndex + NumToMove)) * ElementSize;

		MovedElements.Reset();
		MovedElements.Append(ViewData + MovedStartOffset, MovedSize);
		FMemory::Memmove(ViewData + MovedStartOffset, ViewData + MovedStartOffset + MovedSize, MoveOffset);
		FMemory::Memcpy(ViewData + MovedStartOffset + MoveOffset, MovedElements.GetData(), MovedSize);
	}
}

//-----------------------------------------------------------------------------
// FMassArchetypeCreationParams
//-----------------------------------------------------------------------------
FMassArchetypeCreationParams::FMassArchetypeCreationParams(const FMassArchetypeData& Archetype)
	: ChunkMemorySize(static_cast<int32>(Archetype.GetChunkAllocSize()))
{
}