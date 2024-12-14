// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StructUtils/StructTypeBitSet.h"
#include "MassProcessingTypes.h"
#include "StructUtils/StructArrayView.h"
#include "Subsystems/Subsystem.h"
#include "MassExternalSubsystemTraits.h"
#include "StructUtils/SharedStruct.h"
#include "MassEntityTypes.generated.h"
#ifdef WITH_AITESTSUITE
#include "TestableEnsures.h"
#else
#define testableEnsureMsgf ensureMsgf
#define testableCheckf checkf
#define testableCheckfReturn(InExpression, ReturnValue, InFormat, ... ) checkf(InExpression, InFormat, ##__VA_ARGS__)
#endif 


MASSENTITY_API DECLARE_LOG_CATEGORY_EXTERN(LogMass, Warning, All);

DECLARE_STATS_GROUP(TEXT("Mass"), STATGROUP_Mass, STATCAT_Advanced);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Mass Total Frame Time"), STAT_Mass_Total, STATGROUP_Mass, MASSENTITY_API);

// This is the base class for all lightweight fragments
USTRUCT()
struct FMassFragment
{
	GENERATED_BODY()

	FMassFragment() {}
};

// This is the base class for types that will only be tested for presence/absence, i.e. Tags.
// Subclasses should never contain any member properties.
USTRUCT()
struct FMassTag
{
	GENERATED_BODY()

	FMassTag() {}
};

USTRUCT()
struct FMassChunkFragment
{
	GENERATED_BODY()

	FMassChunkFragment() {}
};

USTRUCT()
struct FMassSharedFragment
{
	GENERATED_BODY()

	FMassSharedFragment() {}
};

USTRUCT()
struct FMassConstSharedFragment
{
	GENERATED_BODY()

	FMassConstSharedFragment() {}
};


// A handle to a lightweight entity.  An entity is used in conjunction with the FMassEntityManager
// for the current world and can contain lightweight fragments.
USTRUCT()
struct alignas(8) FMassEntityHandle
{
	GENERATED_BODY()

	FMassEntityHandle() = default;
	FMassEntityHandle(const int32 InIndex, const int32 InSerialNumber)
		: Index(InIndex), SerialNumber(InSerialNumber)
	{
	}
	
	UPROPERTY(VisibleAnywhere, Category = "Mass|Debug", Transient)
	int32 Index = 0;
	
	UPROPERTY(VisibleAnywhere, Category = "Mass|Debug", Transient)
	int32 SerialNumber = 0;

	bool operator==(const FMassEntityHandle Other) const
	{
		return Index == Other.Index && SerialNumber == Other.SerialNumber;
	}

	bool operator!=(const FMassEntityHandle Other) const
	{
		return !operator==(Other);
	}

	/** Has meaning only for sorting purposes */
	bool operator<(const FMassEntityHandle Other) const { return Index < Other.Index; }

	/** Note that this function is merely checking if Index and SerialNumber are set. There's no way to validate if 
	 *  these indicate a valid entity in an EntitySubsystem without asking the system. */
	bool IsSet() const
	{
		return Index != 0 && SerialNumber != 0;
	}

	FORCEINLINE bool IsValid() const
	{
		return IsSet();
	}

	void Reset()
	{
		Index = SerialNumber = 0;
	}

	/** Allows the entity handle to be shared anonymously. */
	uint64 AsNumber() const { return *reinterpret_cast<const uint64*>(this); } // Relying on the fact that this struct only stores 2 integers and is aligned correctly.
	/** Reconstruct the entity handle from an anonymously shared integer. */
	static FMassEntityHandle FromNumber(uint64 Value) 
	{ 
		FMassEntityHandle Result;
		*reinterpret_cast<uint64_t*>(&Result) = Value;
		return Result;
	}

	friend uint32 GetTypeHash(const FMassEntityHandle Entity)
	{
		return HashCombine(Entity.Index, Entity.SerialNumber);
	}

	FString DebugGetDescription() const
	{
		return FString::Printf(TEXT("i: %d sn: %d"), Index, SerialNumber);
	}
};

static_assert(sizeof(FMassEntityHandle) == sizeof(uint64), "Expected FMassEntityHandle to be convertable to a 64-bit integer value, so size needs to be 8 bytes.");
static_assert(alignof(FMassEntityHandle) == sizeof(uint64), "Expected FMassEntityHandle to be convertable to a 64-bit integer value, so alignment needs to be 8 bytes.");

DECLARE_STRUCTTYPEBITSET_EXPORTED(MASSENTITY_API, FMassFragmentBitSet, FMassFragment);
DECLARE_STRUCTTYPEBITSET_EXPORTED(MASSENTITY_API, FMassTagBitSet, FMassTag);
DECLARE_STRUCTTYPEBITSET_EXPORTED(MASSENTITY_API, FMassChunkFragmentBitSet, FMassChunkFragment);
DECLARE_STRUCTTYPEBITSET_EXPORTED(MASSENTITY_API, FMassSharedFragmentBitSet, FMassSharedFragment);
DECLARE_STRUCTTYPEBITSET_EXPORTED(MASSENTITY_API, FMassConstSharedFragmentBitSet, FMassConstSharedFragment);
DECLARE_CLASSTYPEBITSET_EXPORTED(MASSENTITY_API, FMassExternalSubsystemBitSet, USubsystem);

/** The type summarily describing a composition of an entity or an archetype. It contains information on both the
 *  fragments as well as tags */
struct FMassArchetypeCompositionDescriptor
{
	FMassArchetypeCompositionDescriptor() = default;
	FMassArchetypeCompositionDescriptor(const FMassFragmentBitSet& InFragments,
		const FMassTagBitSet& InTags,
		const FMassChunkFragmentBitSet& InChunkFragments,
		const FMassSharedFragmentBitSet& InSharedFragments,
		const FMassConstSharedFragmentBitSet& InConstSharedFragments)
		: Fragments(InFragments)
		, Tags(InTags)
		, ChunkFragments(InChunkFragments)
		, SharedFragments(InSharedFragments)
		, ConstSharedFragments(InConstSharedFragments)
	{}

	FMassArchetypeCompositionDescriptor(TConstArrayView<const UScriptStruct*> InFragments,
		const FMassTagBitSet& InTags,
		const FMassChunkFragmentBitSet& InChunkFragments,
		const FMassSharedFragmentBitSet& InSharedFragments,
		const FMassConstSharedFragmentBitSet& InConstSharedFragments)
		: FMassArchetypeCompositionDescriptor(FMassFragmentBitSet(InFragments), InTags, InChunkFragments, InSharedFragments, InConstSharedFragments)
	{}

	FMassArchetypeCompositionDescriptor(TConstArrayView<FInstancedStruct> InFragmentInstances,
		const FMassTagBitSet& InTags,
		const FMassChunkFragmentBitSet& InChunkFragments,
		const FMassSharedFragmentBitSet& InSharedFragments,
		const FMassConstSharedFragmentBitSet& InConstSharedFragments)
		: FMassArchetypeCompositionDescriptor(FMassFragmentBitSet(InFragmentInstances), InTags, InChunkFragments, InSharedFragments, InConstSharedFragments)
	{}

	FMassArchetypeCompositionDescriptor(FMassFragmentBitSet&& InFragments,
		FMassTagBitSet&& InTags,
		FMassChunkFragmentBitSet&& InChunkFragments,
		FMassSharedFragmentBitSet&& InSharedFragments,
		FMassConstSharedFragmentBitSet&& InConstSharedFragments)
		: Fragments(MoveTemp(InFragments))
		, Tags(MoveTemp(InTags))
		, ChunkFragments(MoveTemp(InChunkFragments))
		, SharedFragments(MoveTemp(InSharedFragments))
		, ConstSharedFragments(MoveTemp(InConstSharedFragments))
	{}

	FMassArchetypeCompositionDescriptor(FMassFragmentBitSet&& InFragments)
		: Fragments(MoveTemp(InFragments))
	{}

	FMassArchetypeCompositionDescriptor(FMassTagBitSet&& InTags)
		: Tags(MoveTemp(InTags))
	{}

	void Reset()
	{
		Fragments.Reset();
		Tags.Reset();
		ChunkFragments.Reset();
		SharedFragments.Reset();
		ConstSharedFragments.Reset();
	}

	bool IsEquivalent(const FMassArchetypeCompositionDescriptor& OtherDescriptor) const
	{
		return Fragments.IsEquivalent(OtherDescriptor.Fragments) &&
			Tags.IsEquivalent(OtherDescriptor.Tags) &&
			ChunkFragments.IsEquivalent(OtherDescriptor.ChunkFragments) &&
			SharedFragments.IsEquivalent(OtherDescriptor.SharedFragments) &&
			ConstSharedFragments.IsEquivalent(OtherDescriptor.ConstSharedFragments);
	}

	bool IsEmpty() const 
	{ 
		return Fragments.IsEmpty() &&
			Tags.IsEmpty() &&
			ChunkFragments.IsEmpty() &&
			SharedFragments.IsEmpty() &&
			ConstSharedFragments.IsEmpty();
	}

	bool HasAll(const FMassArchetypeCompositionDescriptor& OtherDescriptor) const
	{
		return Fragments.HasAll(OtherDescriptor.Fragments) &&
			Tags.HasAll(OtherDescriptor.Tags) &&
			ChunkFragments.HasAll(OtherDescriptor.ChunkFragments) &&
			SharedFragments.HasAll(OtherDescriptor.SharedFragments) &&
			ConstSharedFragments.HasAll(OtherDescriptor.ConstSharedFragments);
	}

	static uint32 CalculateHash(const FMassFragmentBitSet& InFragments, const FMassTagBitSet& InTags
		, const FMassChunkFragmentBitSet& InChunkFragments, const FMassSharedFragmentBitSet& InSharedFragmentBitSet
		, const FMassConstSharedFragmentBitSet& InConstSharedFragmentBitSet)
	{
		const uint32 FragmentsHash = GetTypeHash(InFragments);
		const uint32 TagsHash = GetTypeHash(InTags);
		const uint32 ChunkFragmentsHash = GetTypeHash(InChunkFragments);
		const uint32 SharedFragmentsHash = GetTypeHash(InSharedFragmentBitSet);
		const uint32 ConstSharedFragmentsHash = GetTypeHash(InConstSharedFragmentBitSet);
		return HashCombine(HashCombine(HashCombine(HashCombine(FragmentsHash, TagsHash), ChunkFragmentsHash), SharedFragmentsHash), ConstSharedFragmentsHash);
	}	

	uint32 CalculateHash() const 
	{
		return CalculateHash(Fragments, Tags, ChunkFragments, SharedFragments, ConstSharedFragments);
	}

	int32 CountStoredTypes() const
	{
		return Fragments.CountStoredTypes()
			+ Tags.CountStoredTypes()
			+ ChunkFragments.CountStoredTypes()
			+ SharedFragments.CountStoredTypes()
			+ ConstSharedFragments.CountStoredTypes();
	}

	void DebugOutputDescription(FOutputDevice& Ar) const
	{
#if WITH_MASSENTITY_DEBUG
		if (Fragments.IsEmpty()
			&& Tags.IsEmpty()
			&& ChunkFragments.IsEmpty())
		{
			Ar.Logf(TEXT("Empty"));
			return;
		}

		const bool bAutoLineEnd = Ar.GetAutoEmitLineTerminator();
		Ar.SetAutoEmitLineTerminator(false);

		if (!Fragments.IsEmpty())
		{
			Ar.Logf(TEXT("Fragments:\n"));
			Fragments.DebugGetStringDesc(Ar);
		}

		if (!Tags.IsEmpty())
		{
			Ar.Logf(TEXT("Tags:\n"));
			Tags.DebugGetStringDesc(Ar);
		}

		if (!ChunkFragments.IsEmpty())
		{
			Ar.Logf(TEXT("ChunkFragments:\n"));
			ChunkFragments.DebugGetStringDesc(Ar);
		}

		if (!SharedFragments.IsEmpty())
		{
			Ar.Logf(TEXT("SharedFragments:\n"));
			SharedFragments.DebugGetStringDesc(Ar);
		}

		if (!ConstSharedFragments.IsEmpty())
		{
			Ar.Logf(TEXT("ConstSharedFragments:\n"));
			ConstSharedFragments.DebugGetStringDesc(Ar);
		}

		Ar.SetAutoEmitLineTerminator(bAutoLineEnd);
#endif // WITH_MASSENTITY_DEBUG

	}

	FMassFragmentBitSet Fragments;
	FMassTagBitSet Tags;
	FMassChunkFragmentBitSet ChunkFragments;
	FMassSharedFragmentBitSet SharedFragments;
	FMassConstSharedFragmentBitSet ConstSharedFragments;

	UE_DEPRECATED(5.5, "This FMassArchetypeCompositionDescriptor constructor is deprecated. Please explicitly provide FConstSharedFragmentBitSet.")
	FMassArchetypeCompositionDescriptor(const FMassFragmentBitSet& InFragments, const FMassTagBitSet& InTags, const FMassChunkFragmentBitSet& InChunkFragments, const FMassSharedFragmentBitSet& InSharedFragments)
		: FMassArchetypeCompositionDescriptor(InFragments, InTags, InChunkFragments, InSharedFragments, FMassConstSharedFragmentBitSet())
	{}

	UE_DEPRECATED(5.5, "This FMassArchetypeCompositionDescriptor constructor is deprecated. Please explicitly provide FConstSharedFragmentBitSet.")
	FMassArchetypeCompositionDescriptor(TConstArrayView<const UScriptStruct*> InFragments, const FMassTagBitSet& InTags, const FMassChunkFragmentBitSet& InChunkFragments, const FMassSharedFragmentBitSet& InSharedFragments)
		: FMassArchetypeCompositionDescriptor(FMassFragmentBitSet(InFragments), InTags, InChunkFragments, InSharedFragments, FMassConstSharedFragmentBitSet())
	{}

	UE_DEPRECATED(5.5, "This FMassArchetypeCompositionDescriptor constructor is deprecated. Please explicitly provide FConstSharedFragmentBitSet.")
	FMassArchetypeCompositionDescriptor(TConstArrayView<FInstancedStruct> InFragmentInstances, const FMassTagBitSet& InTags, const FMassChunkFragmentBitSet& InChunkFragments, const FMassSharedFragmentBitSet& InSharedFragments)
		: FMassArchetypeCompositionDescriptor(FMassFragmentBitSet(InFragmentInstances), InTags, InChunkFragments, InSharedFragments, FMassConstSharedFragmentBitSet())
	{}

	UE_DEPRECATED(5.5, "This FMassArchetypeCompositionDescriptor constructor is deprecated. Please explicitly provide FConstSharedFragmentBitSet.")
	FMassArchetypeCompositionDescriptor(FMassFragmentBitSet&& InFragments, FMassTagBitSet&& InTags, FMassChunkFragmentBitSet&& InChunkFragments, FMassSharedFragmentBitSet&& InSharedFragments)
	{
		ensureMsgf(false, TEXT("This constructor is defunct. Please update your implementation based on deprecation warning."));
	}
};

/** 
 * Wrapper for const and non-const shared fragment containers that tracks which struct types it holds (via a FMassSharedFragmentBitSet).
 * Note that having multiple instanced of a given struct type is not supported and Add* functions will fetch the previously 
 * added fragment instead of adding a new one.
 */
struct MASSENTITY_API FMassArchetypeSharedFragmentValues
{
	FMassArchetypeSharedFragmentValues() = default;
	FMassArchetypeSharedFragmentValues(const FMassArchetypeSharedFragmentValues& OtherFragmentValues) = default;
	FMassArchetypeSharedFragmentValues(FMassArchetypeSharedFragmentValues&& OtherFragmentValues) = default;
	FMassArchetypeSharedFragmentValues& operator=(const FMassArchetypeSharedFragmentValues& OtherFragmentValues) = default;
	FMassArchetypeSharedFragmentValues& operator=(FMassArchetypeSharedFragmentValues&& OtherFragmentValues) = default;

	FORCEINLINE bool HasExactFragmentTypesMatch(const FMassSharedFragmentBitSet& InSharedFragmentBitSet, const FMassConstSharedFragmentBitSet& InConstSharedFragmentBitSet) const
	{
		return HasExactSharedFragmentTypesMatch(InSharedFragmentBitSet)
			&& HasExactConstSharedFragmentTypesMatch(InConstSharedFragmentBitSet);
	}

	FORCEINLINE bool HasExactSharedFragmentTypesMatch(const FMassSharedFragmentBitSet& InSharedFragmentBitSet) const
	{
		return SharedFragmentBitSet.IsEquivalent(InSharedFragmentBitSet);
	}

	FORCEINLINE bool HasAllRequiredSharedFragmentTypes(const FMassSharedFragmentBitSet& InSharedFragmentBitSet) const
	{
		return SharedFragmentBitSet.HasAll(InSharedFragmentBitSet);
	}

	FORCEINLINE bool HasExactConstSharedFragmentTypesMatch(const FMassConstSharedFragmentBitSet& InConstSharedFragmentBitSet) const
	{
		return ConstSharedFragmentBitSet.IsEquivalent(InConstSharedFragmentBitSet);
	}

	FORCEINLINE bool HasAllRequiredConstSharedFragmentTypes(const FMassConstSharedFragmentBitSet& InConstSharedFragmentBitSet) const
	{
		return ConstSharedFragmentBitSet.HasAll(InConstSharedFragmentBitSet);
	}

	FORCEINLINE bool IsEquivalent(const FMassArchetypeSharedFragmentValues& OtherSharedFragmentValues) const
	{
		return GetTypeHash(*this) == GetTypeHash(OtherSharedFragmentValues);
	}

	/** 
	 * Compares contents of `this` and the Other, and allows different order of elements in both containers.
	 * Note that the function ignores "nulls", i.e. empty FConstSharedStruct and FSharedStruct instances. The function
	 * does care however about matching "mode", meaning ConstSharedFragments and SharedFragments arrays are compared
	 * independently.
	 */
	bool HasSameValues(const FMassArchetypeSharedFragmentValues& Other) const;

	FORCEINLINE bool ContainsType(const UScriptStruct* FragmentType) const
	{
		if (FragmentType)
		{
			if (FragmentType->IsChildOf(FMassSharedFragment::StaticStruct()))
			{
				return SharedFragmentBitSet.Contains(*FragmentType);
			}

			if (FragmentType->IsChildOf(FMassConstSharedFragment::StaticStruct()))
			{
				return ConstSharedFragmentBitSet.Contains(*FragmentType);
			}
		}

		return false;
	}

	template<typename T>
	FORCEINLINE bool ContainsType() const
	{
		if constexpr (TIsDerivedFrom<T, FMassConstSharedFragment>::IsDerived)
		{
			return ConstSharedFragmentBitSet.Contains(*T::StaticStruct());
		}
		else if constexpr (TIsDerivedFrom<T, FMassSharedFragment>::IsDerived)
		{
			return SharedFragmentBitSet.Contains(*T::StaticStruct());
		}
		else
		{
			return false;
		}
	}

	/** 
	 * Adds Fragment to the collection. If a fragment of the given FMassSharedFragment subclass has already added then 
	 * the function will return the previously added instance. In that case the function will also assert if the given type 
	 * has been added as a CONST shared fragment and if not it will return an empty FConstSharedStruct
	 */
	FConstSharedStruct AddConstSharedFragment(const FConstSharedStruct& Fragment);

	/**
	 * Adds Fragment to the collection. If a fragment of the given FMassSharedFragment subclass has already added then
	 * the function will return the previously added instance. In that case the function will also assert if the given type
	 * has been added as a NON-CONST shared fragment and if not it will return an empty FSharedStruct
	 */
	FSharedStruct AddSharedFragment(const FSharedStruct& Fragment);

	/**
	 * Finds instances of fragment types given by Fragments and replaces their values with contents of respective
	 * element of Fragments.
	 * Note that it's callers responsibility to ensure every fragment type in Fragments already has an instance in
	 * this FMassArchetypeSharedFragmentValues instance. Failing that assumption will result in ensure failure. 
	 */
	void ReplaceSharedFragments(TConstArrayView<FSharedStruct> Fragments);

	/** 
	 * Appends contents of Other to `this` instance. All common fragments will get overridden with values in Other.
	 * Note that changing a fragments "role" (being const or non-const) is not supported and the function will fail an
	 * ensure when that is attempted.
	 * @return number of fragments added or changed
	 */
	int32 Append(const FMassArchetypeSharedFragmentValues& Other);

	/** 
	 * Note that the function removes the shared fragments by type
	 * @return number of fragments types removed
	 */
	int32 Remove(const FMassSharedFragmentBitSet& SharedFragmentToRemoveBitSet);

	/** 
	 * Note that the function removes the const shared fragments by type
	 * @return number of fragments types removed
	 */
	int32 Remove(const FMassConstSharedFragmentBitSet& ConstSharedFragmentToRemoveBitSet);

	FORCEINLINE const TArray<FConstSharedStruct>& GetConstSharedFragments() const
	{
		return ConstSharedFragments;
	}

	FORCEINLINE TArray<FSharedStruct>& GetMutableSharedFragments()
	{
		return SharedFragments;
	}
	
	FORCEINLINE const TArray<FSharedStruct>& GetSharedFragments() const
	{
		return SharedFragments;
	}
	
	FConstSharedStruct GetConstSharedFragmentStruct(const UScriptStruct* StructType) const
	{
		const int32 FragmentIndex = ConstSharedFragments.IndexOfByPredicate(FStructTypeEqualOperator(StructType));
		return FragmentIndex != INDEX_NONE ? ConstSharedFragments[FragmentIndex] : FConstSharedStruct();
	}
		
	FSharedStruct GetSharedFragmentStruct(const UScriptStruct* StructType)
	{
		const int32 FragmentIndex = SharedFragments.IndexOfByPredicate(FStructTypeEqualOperator(StructType));
		return FragmentIndex != INDEX_NONE ? SharedFragments[FragmentIndex] : FSharedStruct();
	}

	const FMassSharedFragmentBitSet& GetSharedFragmentBitSet() const
	{
		return SharedFragmentBitSet;
	}

	const FMassConstSharedFragmentBitSet& GetConstSharedFragmentBitSet() const
	{
		return ConstSharedFragmentBitSet;
	}

	FORCEINLINE void DirtyHashCache()
	{
		HashCache = UINT32_MAX;
		// we consider a single shared fragment as being "sorted"
		bSorted = (SharedFragments.Num() + ConstSharedFragments.Num() <= 1) ;
	}

	FORCEINLINE void CacheHash() const
	{
		if (HashCache == UINT32_MAX)
		{
			HashCache = CalculateHash();
		}
	}

	friend FORCEINLINE uint32 GetTypeHash(const FMassArchetypeSharedFragmentValues& SharedFragmentValues)
	{
		SharedFragmentValues.CacheHash();
		return SharedFragmentValues.HashCache;
	}

	uint32 CalculateHash() const;

	SIZE_T GetAllocatedSize() const
	{
		return ConstSharedFragments.GetAllocatedSize() + SharedFragments.GetAllocatedSize();
	}

	void Sort()
	{
		if(!bSorted)
		{
			ConstSharedFragments.Sort(FStructTypeSortOperator());
			SharedFragments.Sort(FStructTypeSortOperator());
			bSorted = true;
		}
	}

	bool IsSorted() const { return bSorted; }

protected:
	mutable uint32 HashCache = UINT32_MAX;
	/**
	 * We consider empty FMassArchetypeSharedFragmentValues a sorted containter.Same goes for a container containing
	 * a single element, @see DirtyHashCache
	 */ 
	mutable bool bSorted = true; 
	
	FMassSharedFragmentBitSet SharedFragmentBitSet;
	FMassConstSharedFragmentBitSet ConstSharedFragmentBitSet;
	TArray<FConstSharedStruct> ConstSharedFragments;
	TArray<FSharedStruct> SharedFragments;

public:
	//-----------------------------------------------------------------------------
	// DEPRECATED
	//-----------------------------------------------------------------------------
	UE_DEPRECATED(5.5, "HasExactFragmentTypesMatch is deprecated. Use HasExactSharedFragmentTypesMatch or the two-parameter version of HasExactFragmentTypesMatch.")
	FORCEINLINE bool HasExactFragmentTypesMatch(const FMassSharedFragmentBitSet& InSharedFragmentBitSet) const
	{
		return HasExactSharedFragmentTypesMatch(InSharedFragmentBitSet);
	}
};

UENUM()
enum class EMassObservedOperation : uint8
{
	Add,
	Remove,
	// @todo Keeping this here as a indication of design intent. For now we handle entity destruction like removal, but 
	// there might be computationally expensive cases where we might want to avoid for soon-to-be-dead entities. 
	// Destroy,
	// @todo another planned supported operation type
	// Touch,
	MAX
};

enum class EMassExecutionContextType : uint8
{
	Local,
	Processor,
	MAX
};

/** 
 * Note that this is a view and is valid only as long as the source data is valid. Used when flushing mass commands to
 * wrap different kinds of data into a uniform package so that it can be passed over to a common interface.
 */
struct FMassGenericPayloadView
{
	FMassGenericPayloadView() = default;
	FMassGenericPayloadView(TArray<FStructArrayView>&SourceData)
		: Content(SourceData)
	{}
	FMassGenericPayloadView(TArrayView<FStructArrayView> SourceData)
		: Content(SourceData)
	{}

	int32 Num() const { return Content.Num(); }

	void Reset()
	{
		Content = TArrayView<FStructArrayView>();
	}

	FORCEINLINE void Swap(const int32 A, const int32 B)
	{
		for (FStructArrayView& View : Content)
		{
			View.Swap(A, B);
		}
	}

	/** Moves NumToMove elements to the back of the viewed collection. */
	void SwapElementsToEnd(int32 StartIndex, int32 NumToMove);

	TArrayView<FStructArrayView> Content;
};

/**
 * Used to indicate a specific slice of a preexisting FMassGenericPayloadView, it's essentially an access pattern
 * Note: accessing content generates copies of FStructArrayViews stored (still cheap, those are just views). 
 */
struct FMassGenericPayloadViewSlice
{
	FMassGenericPayloadViewSlice() = default;
	FMassGenericPayloadViewSlice(const FMassGenericPayloadView& InSource, const int32 InStartIndex, const int32 InCount)
		: Source(InSource), StartIndex(InStartIndex), Count(InCount)
	{
	}

	FStructArrayView operator[](const int32 Index) const
	{
		return Source.Content[Index].Slice(StartIndex, Count);
	}

	/** @return the number of "layers" (i.e. number of original arrays) this payload has been built from */
	int32 Num() const 
	{
		return Source.Num();
	}

	bool IsEmpty() const
	{
		return !(Source.Num() > 0 && Count > 0);
	}

private:
	FMassGenericPayloadView Source;
	const int32 StartIndex = 0;
	const int32 Count = 0;
};

namespace UE::Mass
{
	/**
	 * A statically-typed list of of related types. Used mainly to differentiate type collections at compile-type as well as
	 * efficiently produce TStructTypeBitSet representing given collection.
	 */
	template<typename T, typename... TOthers>
	struct TMultiTypeList : TMultiTypeList<TOthers...>
	{
		using Super = TMultiTypeList<TOthers...>;
		using FType = std::remove_const_t<typename TRemoveReference<T>::Type>;
		enum
		{
			Ordinal = Super::Ordinal + 1
		};

		template<typename TBitSetType>
		constexpr static void PopulateBitSet(TBitSetType& OutBitSet)
		{
			Super::PopulateBitSet(OutBitSet);
			OutBitSet += TBitSetType::template GetTypeBitSet<FType>();
		}
	};
		
	/** Single-type specialization of TMultiTypeList. */
	template<typename T>
	struct TMultiTypeList<T>
	{
		using FType = std::remove_const_t<typename TRemoveReference<T>::Type>;
		enum
		{
			Ordinal = 0
		};

		template<typename TBitSetType>
		constexpr static void PopulateBitSet(TBitSetType& OutBitSet)
		{
			OutBitSet += TBitSetType::template GetTypeBitSet<FType>();
		}
	};

	/** 
	 * The type hosts a statically-typed collection of TArrays, where each TArray is strongly-typed (i.e. it contains 
	 * instances of given structs rather than structs wrapped up in FInstancedStruct). This type lets us do batched 
	 * fragment values setting by simply copying data rather than setting per-instance. 
	 */
	template<typename T, typename... TOthers>
	struct TMultiArray : TMultiArray<TOthers...>
	{
		using FType = std::remove_const_t<typename TRemoveReference<T>::Type>;
		using Super = TMultiArray<TOthers...>;

		enum
		{
			Ordinal = Super::Ordinal + 1
		};

		SIZE_T GetAllocatedSize() const
		{
			return FragmentInstances.GetAllocatedSize() + Super::GetAllocatedSize();
		}

		int GetNumArrays() const { return Ordinal + 1; }

		void Add(const FType& Item, TOthers... Rest)
		{
			FragmentInstances.Add(Item);
			Super::Add(Rest...);
		}

		void GetAsGenericMultiArray(TArray<FStructArrayView>& A) /*const*/
		{
			Super::GetAsGenericMultiArray(A);
			A.Add(FStructArrayView(FragmentInstances));
		}

		void GetheredAffectedFragments(FMassFragmentBitSet& OutBitSet) const
		{
			Super::GetheredAffectedFragments(OutBitSet);
			OutBitSet += FMassFragmentBitSet::GetTypeBitSet<FType>();
		}

		void Reset()
		{
			Super::Reset();
			FragmentInstances.Reset();
		}

		TArray<FType> FragmentInstances;
	};

	/**TMultiArray simple-type specialization */
	template<typename T>
	struct TMultiArray<T>
	{
		using FType = std::remove_const_t<typename TRemoveReference<T>::Type>;
		enum { Ordinal = 0 };

		SIZE_T GetAllocatedSize() const
		{
			return FragmentInstances.GetAllocatedSize();
		}

		int GetNumArrays() const { return Ordinal + 1; }

		void Add(const FType& Item) { FragmentInstances.Add(Item); }

		void GetAsGenericMultiArray(TArray<FStructArrayView>& A) /*const*/
		{
			A.Add(FStructArrayView(FragmentInstances));
		}

		void GetheredAffectedFragments(FMassFragmentBitSet& OutBitSet) const
		{
			OutBitSet += FMassFragmentBitSet::GetTypeBitSet<FType>();
		}

		void Reset()
		{
			FragmentInstances.Reset();
		}

		TArray<FType> FragmentInstances;
	};

} // UE::Mass


struct FMassArchetypeCreationParams
{
	FMassArchetypeCreationParams() = default;
	explicit FMassArchetypeCreationParams(const struct FMassArchetypeData& Archetype);

	/** Created archetype will have chunks of this size. 0 denotes "use default" (see UE::Mass::ChunkSize) */
	int32 ChunkMemorySize = 0;

	/** Name to identify the archetype while debugging*/
	FName DebugName;
};
