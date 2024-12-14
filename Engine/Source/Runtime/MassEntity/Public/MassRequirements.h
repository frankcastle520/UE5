// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntityTypes.h"
#include "MassRequirements.generated.h"

struct FMassDebugger;
struct FMassArchetypeHandle;
struct FMassExecutionRequirements;
struct FMassRequirementAccessDetector;

UENUM()
enum class EMassFragmentAccess : uint8
{
	/** no binding required */
	None, 

	/** We want to read the data for the fragment */
	ReadOnly,

	/** We want to read and write the data for the fragment */
	ReadWrite,

	MAX
};

UENUM()
enum class EMassFragmentPresence : uint8
{
	/** All of the required fragments must be present */
	All,

	/** One of the required fragments must be present */
	Any,

	/** None of the required fragments can be present */
	None,

	/** If fragment is present we'll use it, but it missing stop processing of a given archetype */
	Optional,

	MAX
};


struct MASSENTITY_API FMassFragmentRequirementDescription
{
	const UScriptStruct* StructType = nullptr;
	EMassFragmentAccess AccessMode = EMassFragmentAccess::None;
	EMassFragmentPresence Presence = EMassFragmentPresence::Optional;

public:
	FMassFragmentRequirementDescription(){}
	FMassFragmentRequirementDescription(const UScriptStruct* InStruct, const EMassFragmentAccess InAccessMode, const EMassFragmentPresence InPresence)
		: StructType(InStruct)
		, AccessMode(InAccessMode)
		, Presence(InPresence)
	{
		check(InStruct);
	}

	bool RequiresBinding() const { return (AccessMode != EMassFragmentAccess::None); }
	bool IsOptional() const { return (Presence == EMassFragmentPresence::Optional || Presence == EMassFragmentPresence::Any); }

	/** these functions are used for sorting. See FScriptStructSortOperator */
	int32 GetStructureSize() const
	{
		return StructType->GetStructureSize();
	}

	FName GetFName() const
	{
		return StructType->GetFName();
	}
};

/**
 *  FMassSubsystemRequirements is a structure that declares runtime subsystem access type given calculations require.
 */
USTRUCT()
struct MASSENTITY_API FMassSubsystemRequirements
{
	GENERATED_BODY()

	friend FMassDebugger;
	friend FMassRequirementAccessDetector;

	template<typename T>
	FMassSubsystemRequirements& AddSubsystemRequirement(const EMassFragmentAccess AccessMode)
	{
		check(AccessMode != EMassFragmentAccess::None && AccessMode != EMassFragmentAccess::MAX);

		// Compilation errors here like: 'GameThreadOnly': is not a member of 'TMassExternalSubsystemTraits<USmartObjectSubsystem>
		// indicate that there is a missing header that defines the subsystem's trait or that you need to define one for that subsystem type.
		// @see "MassExternalSubsystemTraits.h" for details

		switch (AccessMode)
		{
		case EMassFragmentAccess::ReadOnly:
			RequiredConstSubsystems.Add<T>();
			bRequiresGameThreadExecution |= TMassExternalSubsystemTraits<T>::GameThreadOnly;
			break;
		case EMassFragmentAccess::ReadWrite:
			RequiredMutableSubsystems.Add<T>();
			bRequiresGameThreadExecution |= TMassExternalSubsystemTraits<T>::GameThreadOnly;
			break;
		default:
			check(false);
		}

		return *this;
	}

	FMassSubsystemRequirements& AddSubsystemRequirement(const TSubclassOf<USubsystem> SubsystemClass, const EMassFragmentAccess AccessMode, const bool bGameThreadOnly = true)
	{
		check(AccessMode != EMassFragmentAccess::None && AccessMode != EMassFragmentAccess::MAX);

		switch (AccessMode)
		{
		case EMassFragmentAccess::ReadOnly:
			RequiredConstSubsystems.Add(**SubsystemClass);
			bRequiresGameThreadExecution |= bGameThreadOnly;
			break;
		case EMassFragmentAccess::ReadWrite:
			RequiredMutableSubsystems.Add(**SubsystemClass);
			bRequiresGameThreadExecution |= bGameThreadOnly;
			break;
		default:
			check(false);
		}

		return *this;
	}

	void Reset();

	const FMassExternalSubsystemBitSet& GetRequiredConstSubsystems() const { return RequiredConstSubsystems; }
	const FMassExternalSubsystemBitSet& GetRequiredMutableSubsystems() const { return RequiredMutableSubsystems; }
	bool IsEmpty() const { return RequiredConstSubsystems.IsEmpty() && RequiredMutableSubsystems.IsEmpty(); }

	bool DoesRequireGameThreadExecution() const { return bRequiresGameThreadExecution; }
	void ExportRequirements(FMassExecutionRequirements& OutRequirements) const;

protected:
	FMassExternalSubsystemBitSet RequiredConstSubsystems;
	FMassExternalSubsystemBitSet RequiredMutableSubsystems;

private:
	bool bRequiresGameThreadExecution = false;
};

/** 
 *  FMassFragmentRequirements is a structure that describes properties required of an archetype that's a subject of calculations.
 */
USTRUCT()
struct MASSENTITY_API FMassFragmentRequirements
{
	GENERATED_BODY()

	friend FMassDebugger;
	friend FMassRequirementAccessDetector;

public:
	FMassFragmentRequirements(){}
	FMassFragmentRequirements(std::initializer_list<UScriptStruct*> InitList);
	FMassFragmentRequirements(TConstArrayView<const UScriptStruct*> InitList);

	FMassFragmentRequirements& AddRequirement(const UScriptStruct* FragmentType, const EMassFragmentAccess AccessMode, const EMassFragmentPresence Presence = EMassFragmentPresence::All)
	{
		checkf(FragmentRequirements.FindByPredicate([FragmentType](const FMassFragmentRequirementDescription& Item){ return Item.StructType == FragmentType; }) == nullptr
			, TEXT("Duplicated requirements are not supported. %s already present"), *GetNameSafe(FragmentType));
		
		if (Presence != EMassFragmentPresence::None)
		{
			FragmentRequirements.Emplace(FragmentType, AccessMode, Presence);
		}

		switch (Presence)
		{
		case EMassFragmentPresence::All:
			RequiredAllFragments.Add(*FragmentType);
			break;
		case EMassFragmentPresence::Any:
			RequiredAnyFragments.Add(*FragmentType);
			break;
		case EMassFragmentPresence::Optional:
			RequiredOptionalFragments.Add(*FragmentType);
			break;
		case EMassFragmentPresence::None:
			RequiredNoneFragments.Add(*FragmentType);
			break;
		}
		// force recaching the next time this query is used or the following CacheArchetypes call.
		IncrementChangeCounter();
		return *this;
	}

	/** FMassFragmentRequirements ref returned for chaining */
	template<typename T>
	FMassFragmentRequirements& AddRequirement(const EMassFragmentAccess AccessMode, const EMassFragmentPresence Presence = EMassFragmentPresence::All)
	{
		checkf(FragmentRequirements.FindByPredicate([](const FMassFragmentRequirementDescription& Item) { return Item.StructType == T::StaticStruct(); }) == nullptr
			, TEXT("Duplicated requirements are not supported. %s already present"), *T::StaticStruct()->GetName());

		static_assert(TIsDerivedFrom<T, FMassFragment>::IsDerived, "Given struct doesn't represent a valid fragment type. Make sure to inherit from FMassFragment or one of its child-types.");
		
		if (Presence != EMassFragmentPresence::None)
		{
			FragmentRequirements.Emplace(T::StaticStruct(), AccessMode, Presence);
		}
		
		switch (Presence)
		{
		case EMassFragmentPresence::All:
			RequiredAllFragments.Add<T>();
			break;
		case EMassFragmentPresence::Any:
			RequiredAnyFragments.Add<T>();
			break;
		case EMassFragmentPresence::Optional:
			RequiredOptionalFragments.Add<T>();
			break;
		case EMassFragmentPresence::None:
			RequiredNoneFragments.Add<T>();
			break;
		}
		// force recaching the next time this query is used or the following CacheArchetypes call.
		IncrementChangeCounter();
		return *this;
	}

	void AddTagRequirement(const UScriptStruct& TagType, const EMassFragmentPresence Presence)
	{
		checkf(int(Presence) != int(EMassFragmentPresence::MAX), TEXT("MAX presence is not a valid value for AddTagRequirement"));
		switch (Presence)
		{
		case EMassFragmentPresence::All:
			RequiredAllTags.Add(TagType);
			break;
		case EMassFragmentPresence::Any:
			RequiredAnyTags.Add(TagType);
			break;
		case EMassFragmentPresence::None:
			RequiredNoneTags.Add(TagType);
			break;
		case EMassFragmentPresence::Optional:
			RequiredOptionalTags.Add(TagType);
			break;
		}
		IncrementChangeCounter();
	}

	template<typename T>
	FMassFragmentRequirements& AddTagRequirement(const EMassFragmentPresence Presence)
	{
		checkf(int(Presence) != int(EMassFragmentPresence::MAX), TEXT("MAX presence is not a valid value for AddTagRequirement"));
		static_assert(TIsDerivedFrom<T, FMassTag>::IsDerived, "Given struct doesn't represent a valid tag type. Make sure to inherit from FMassFragment or one of its child-types.");
		switch (Presence)
		{
			case EMassFragmentPresence::All:
				RequiredAllTags.Add<T>();
				break;
			case EMassFragmentPresence::Any:
				RequiredAnyTags.Add<T>();
				break;
			case EMassFragmentPresence::None:
				RequiredNoneTags.Add<T>();
				break;
			case EMassFragmentPresence::Optional:
				RequiredOptionalTags.Add<T>();
				break;
		}
		IncrementChangeCounter();
		return *this;
	}

	/** actual implementation in specializations */
	template<EMassFragmentPresence Presence> 
	FMassFragmentRequirements& AddTagRequirements(const FMassTagBitSet& TagBitSet)
	{
		static_assert(Presence == EMassFragmentPresence::None || Presence == EMassFragmentPresence::All || Presence == EMassFragmentPresence::Any
			, "The only valid values for AddTagRequirements are All, Any and None");
		return *this;
	}

	/** Clears given tags out of all collected requirements, including negative ones */
	FMassFragmentRequirements& ClearTagRequirements(const FMassTagBitSet& TagsToRemoveBitSet);

	template<typename T>
	FMassFragmentRequirements& AddChunkRequirement(const EMassFragmentAccess AccessMode, const EMassFragmentPresence Presence = EMassFragmentPresence::All)
	{
		static_assert(TIsDerivedFrom<T, FMassChunkFragment>::IsDerived, "Given struct doesn't represent a valid chunk fragment type. Make sure to inherit from FMassChunkFragment or one of its child-types.");
		checkf(ChunkFragmentRequirements.FindByPredicate([](const FMassFragmentRequirementDescription& Item) { return Item.StructType == T::StaticStruct(); }) == nullptr
			, TEXT("Duplicated requirements are not supported. %s already present"), *T::StaticStruct()->GetName());
		checkf(Presence != EMassFragmentPresence::Any, TEXT("\'Any\' is not a valid Presence value for AddChunkRequirement."));

		switch (Presence)
		{
			case EMassFragmentPresence::All:
				RequiredAllChunkFragments.Add<T>();
				ChunkFragmentRequirements.Emplace(T::StaticStruct(), AccessMode, Presence);
				break;
			case EMassFragmentPresence::Optional:
				RequiredOptionalChunkFragments.Add<T>();
				ChunkFragmentRequirements.Emplace(T::StaticStruct(), AccessMode, Presence);
				break;
			case EMassFragmentPresence::None:
				RequiredNoneChunkFragments.Add<T>();
				break;
		}
		IncrementChangeCounter();
		return *this;
	}

	template<typename T>
	FMassFragmentRequirements& AddConstSharedRequirement(const EMassFragmentPresence Presence = EMassFragmentPresence::All)
	{
		static_assert(TIsDerivedFrom<T, FMassConstSharedFragment>::IsDerived, "Given struct doesn't represent a valid const shared fragment type. Make sure to inherit from FMassConstSharedFragment or one of its child-types.");
		checkf(ConstSharedFragmentRequirements.FindByPredicate([](const FMassFragmentRequirementDescription& Item) { return Item.StructType == T::StaticStruct(); }) == nullptr
			, TEXT("Duplicated requirements are not supported. %s already present"), *T::StaticStruct()->GetName());
		checkf(Presence != EMassFragmentPresence::Any, TEXT("\'Any\' is not a valid Presence value for AddConstSharedRequirement."));

		switch (Presence)
		{
		case EMassFragmentPresence::All:
			RequiredAllConstSharedFragments.Add<T>();
			ConstSharedFragmentRequirements.Emplace(T::StaticStruct(), EMassFragmentAccess::ReadOnly, Presence);
			break;
		case EMassFragmentPresence::Optional:
			RequiredOptionalConstSharedFragments.Add<T>();
			ConstSharedFragmentRequirements.Emplace(T::StaticStruct(), EMassFragmentAccess::ReadOnly, Presence);
			break;
		case EMassFragmentPresence::None:
			RequiredNoneConstSharedFragments.Add<T>();
			break;
		}
		IncrementChangeCounter();
		return *this;
	}

	FMassFragmentRequirements& AddConstSharedRequirement(const UScriptStruct* FragmentType, const EMassFragmentPresence Presence = EMassFragmentPresence::All)
	{
		if (!ensureMsgf(FragmentType->IsChildOf(FMassConstSharedFragment::StaticStruct())
			, TEXT("Given struct doesn't represent a valid const shared fragment type. Make sure to inherit from FMassConstSharedFragment or one of its child-types.")))
		{
			return *this;
		}

		checkf(ConstSharedFragmentRequirements.FindByPredicate([FragmentType](const FMassFragmentRequirementDescription& Item) { return Item.StructType == FragmentType; }) == nullptr
			, TEXT("Duplicated requirements are not supported. %s already present"), *FragmentType->GetName());
		checkf(Presence != EMassFragmentPresence::Any, TEXT("\'Any\' is not a valid Presence value for AddConstSharedRequirement."));

		switch (Presence)
		{
		case EMassFragmentPresence::All:
			RequiredAllConstSharedFragments.Add(*FragmentType);
			ConstSharedFragmentRequirements.Emplace(FragmentType, EMassFragmentAccess::ReadOnly, Presence);
			break;
		case EMassFragmentPresence::Optional:
			RequiredOptionalConstSharedFragments.Add(*FragmentType);
			ConstSharedFragmentRequirements.Emplace(FragmentType, EMassFragmentAccess::ReadOnly, Presence);
			break;
		case EMassFragmentPresence::None:
			RequiredNoneConstSharedFragments.Add(*FragmentType);
			break;
		}
		IncrementChangeCounter();
		return *this;
	}

	template<typename T>
	FMassFragmentRequirements& AddSharedRequirement(const EMassFragmentAccess AccessMode, const EMassFragmentPresence Presence = EMassFragmentPresence::All)
	{
		static_assert(TIsDerivedFrom<T, FMassSharedFragment>::IsDerived, "Given struct doesn't represent a valid shared fragment type. Make sure to inherit from FMassSharedFragment or one of its child-types.");
		checkf(SharedFragmentRequirements.FindByPredicate([](const FMassFragmentRequirementDescription& Item) { return Item.StructType == T::StaticStruct(); }) == nullptr
			, TEXT("Duplicated requirements are not supported. %s already present"), *T::StaticStruct()->GetName());
		checkf(Presence != EMassFragmentPresence::Any, TEXT("\'Any\' is not a valid Presence value for AddSharedRequirement."));

		switch (Presence)
		{
		case EMassFragmentPresence::All:
			RequiredAllSharedFragments.Add<T>();
			SharedFragmentRequirements.Emplace(T::StaticStruct(), AccessMode, Presence);
			if (AccessMode == EMassFragmentAccess::ReadWrite)
			{
				bRequiresGameThreadExecution |= TMassSharedFragmentTraits<T>::GameThreadOnly;
			}
			break;
		case EMassFragmentPresence::Optional:
			RequiredOptionalSharedFragments.Add<T>();
			SharedFragmentRequirements.Emplace(T::StaticStruct(), AccessMode, Presence);
			if (AccessMode == EMassFragmentAccess::ReadWrite)
			{
				bRequiresGameThreadExecution |= TMassSharedFragmentTraits<T>::GameThreadOnly;
			}
			break;
		case EMassFragmentPresence::None:
			RequiredNoneSharedFragments.Add<T>();
			break;
		}
		IncrementChangeCounter();
		return *this;
	}

	void Reset();

	/** 
	 * The function validates requirements we make for queries. See the FMassFragmentRequirements struct description for details.
	 * Even though the code of the function is non trivial the consecutive calls will be essentially free due to the result 
	 * being cached (note that the caching gets invalidated if the composition changes).
	 * @return whether this query's requirements follow the rules.
	 */
	bool CheckValidity() const;

	TConstArrayView<FMassFragmentRequirementDescription> GetFragmentRequirements() const { return FragmentRequirements; }
	TConstArrayView<FMassFragmentRequirementDescription> GetChunkFragmentRequirements() const { return ChunkFragmentRequirements; }
	TConstArrayView<FMassFragmentRequirementDescription> GetConstSharedFragmentRequirements() const { return ConstSharedFragmentRequirements; }
	TConstArrayView<FMassFragmentRequirementDescription> GetSharedFragmentRequirements() const { return SharedFragmentRequirements; }
	const FMassFragmentBitSet& GetRequiredAllFragments() const { return RequiredAllFragments; }
	const FMassFragmentBitSet& GetRequiredAnyFragments() const { return RequiredAnyFragments; }
	const FMassFragmentBitSet& GetRequiredOptionalFragments() const { return RequiredOptionalFragments; }
	const FMassFragmentBitSet& GetRequiredNoneFragments() const { return RequiredNoneFragments; }
	const FMassTagBitSet& GetRequiredAllTags() const { return RequiredAllTags; }
	const FMassTagBitSet& GetRequiredAnyTags() const { return RequiredAnyTags; }
	const FMassTagBitSet& GetRequiredNoneTags() const { return RequiredNoneTags; }
	const FMassTagBitSet& GetRequiredOptionalTags() const { return RequiredOptionalTags; }
	const FMassChunkFragmentBitSet& GetRequiredAllChunkFragments() const { return RequiredAllChunkFragments; }
	const FMassChunkFragmentBitSet& GetRequiredOptionalChunkFragments() const { return RequiredOptionalChunkFragments; }
	const FMassChunkFragmentBitSet& GetRequiredNoneChunkFragments() const { return RequiredNoneChunkFragments; }
	const FMassSharedFragmentBitSet& GetRequiredAllSharedFragments() const { return RequiredAllSharedFragments; }
	const FMassSharedFragmentBitSet& GetRequiredOptionalSharedFragments() const { return RequiredOptionalSharedFragments; }
	const FMassSharedFragmentBitSet& GetRequiredNoneSharedFragments() const { return RequiredNoneSharedFragments; }
	const FMassConstSharedFragmentBitSet& GetRequiredAllConstSharedFragments() const { return RequiredAllConstSharedFragments; }
	const FMassConstSharedFragmentBitSet& GetRequiredOptionalConstSharedFragments() const { return RequiredOptionalConstSharedFragments; }
	const FMassConstSharedFragmentBitSet& GetRequiredNoneConstSharedFragments() const { return RequiredNoneConstSharedFragments; }

	bool IsEmpty() const;
	bool HasPositiveRequirements() const { return bHasPositiveRequirements; }
	bool HasNegativeRequirements() const { return bHasNegativeRequirements; }
	bool HasOptionalRequirements() const { return bHasOptionalRequirements; }

	bool DoesArchetypeMatchRequirements(const FMassArchetypeHandle& ArchetypeHandle) const;
	bool DoesArchetypeMatchRequirements(const FMassArchetypeCompositionDescriptor& ArchetypeComposition) const;
	bool DoesMatchAnyOptionals(const FMassArchetypeCompositionDescriptor& ArchetypeComposition) const;

	bool DoesRequireGameThreadExecution() const { return bRequiresGameThreadExecution; }
	void ExportRequirements(FMassExecutionRequirements& OutRequirements) const;

protected:
	void SortRequirements();

	FORCEINLINE void IncrementChangeCounter() 
	{ 
		++IncrementalChangesCount; 
		bPropertiesCached = false;
	}
	void ConsumeIncrementalChangesCount() { IncrementalChangesCount = 0; }
	bool HasIncrementalChanges() const{ return IncrementalChangesCount > 0; }

protected:
	friend FMassRequirementAccessDetector;

	TArray<FMassFragmentRequirementDescription> FragmentRequirements;
	TArray<FMassFragmentRequirementDescription> ChunkFragmentRequirements;
	TArray<FMassFragmentRequirementDescription> ConstSharedFragmentRequirements;
	TArray<FMassFragmentRequirementDescription> SharedFragmentRequirements;
	FMassTagBitSet RequiredAllTags;
	FMassTagBitSet RequiredAnyTags;
	FMassTagBitSet RequiredNoneTags;
	/**
	 * note that optional tags have meaning only if there are no other strict requirements, i.e. everything is optional,
	 * so we're looking for anything matching any of the optionals (both tags as well as fragments).
	 */
	FMassTagBitSet RequiredOptionalTags;
	FMassFragmentBitSet RequiredAllFragments;
	FMassFragmentBitSet RequiredAnyFragments;
	FMassFragmentBitSet RequiredOptionalFragments;
	FMassFragmentBitSet RequiredNoneFragments;
	FMassChunkFragmentBitSet RequiredAllChunkFragments;
	FMassChunkFragmentBitSet RequiredOptionalChunkFragments;
	FMassChunkFragmentBitSet RequiredNoneChunkFragments;
	FMassSharedFragmentBitSet RequiredAllSharedFragments;
	FMassSharedFragmentBitSet RequiredOptionalSharedFragments;
	FMassSharedFragmentBitSet RequiredNoneSharedFragments;
	FMassConstSharedFragmentBitSet RequiredAllConstSharedFragments;
	FMassConstSharedFragmentBitSet RequiredOptionalConstSharedFragments;
	FMassConstSharedFragmentBitSet RequiredNoneConstSharedFragments;

private:
	FORCEINLINE void CachePropreties() const;
	mutable uint16 bPropertiesCached : 1 = false;
	mutable uint16 bHasPositiveRequirements : 1 = false;
	mutable uint16 bHasNegativeRequirements : 1 = false;
	/** 
	 * Indicates that the requirements specify only optional elements, which means any composition having any one of 
	 * the optional elements will be accepted. Note that RequiredNone* requirements are handled separately and if specified 
	 * still need to be satisfied.
	 */
	//mutable uint16 bOptionalsOnly : 1 = false;
	mutable uint16 bHasOptionalRequirements : 1 = false;

	uint16 IncrementalChangesCount = 0;

	bool bRequiresGameThreadExecution = false;
};

template<>
FORCEINLINE FMassFragmentRequirements& FMassFragmentRequirements::AddTagRequirements<EMassFragmentPresence::All>(const FMassTagBitSet& TagBitSet)
{
	RequiredAllTags += TagBitSet;
	// force recaching the next time this query is used or the following CacheArchetypes call.
	IncrementChangeCounter();
	return *this;
}

template<>
FORCEINLINE FMassFragmentRequirements& FMassFragmentRequirements::AddTagRequirements<EMassFragmentPresence::Any>(const FMassTagBitSet& TagBitSet)
{
	RequiredAnyTags += TagBitSet;
	// force recaching the next time this query is used or the following CacheArchetypes call.
	IncrementChangeCounter();
	return *this;
}

template<>
FORCEINLINE FMassFragmentRequirements& FMassFragmentRequirements::AddTagRequirements<EMassFragmentPresence::None>(const FMassTagBitSet& TagBitSet)
{
	RequiredNoneTags += TagBitSet;
	// force recaching the next time this query is used or the following CacheArchetypes call.
	IncrementChangeCounter();
	return *this;
}

template<>
FORCEINLINE FMassFragmentRequirements& FMassFragmentRequirements::AddTagRequirements<EMassFragmentPresence::Optional>(const FMassTagBitSet& TagBitSet)
{
	RequiredOptionalTags += TagBitSet;
	// force recaching the next time this query is used or the following CacheArchetypes call.
	IncrementChangeCounter();
	return *this;
}
