// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassRequirements.h"
#include "MassArchetypeData.h"
#include "MassProcessorDependencySolver.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MassRequirements)

#if WITH_MASSENTITY_DEBUG
#include "MassRequirementAccessDetector.h"
#endif // WITH_MASSENTITY_DEBUG


namespace UE::Mass::Private
{
	template<typename TContainer>
	void ExportRequirements(TConstArrayView<FMassFragmentRequirementDescription> Requirements, TMassExecutionAccess<TContainer>& Out)
	{
		for (const FMassFragmentRequirementDescription& Requirement : Requirements)
		{
			if (Requirement.Presence != EMassFragmentPresence::None)
			{
				check(Requirement.StructType);
				if (Requirement.AccessMode == EMassFragmentAccess::ReadOnly)
				{
					Out.Read.Add(*Requirement.StructType);
				}
				else if (Requirement.AccessMode == EMassFragmentAccess::ReadWrite)
				{
					Out.Write.Add(*Requirement.StructType);
				}
			}
		}
	}

	template<>
	void ExportRequirements<FMassConstSharedFragmentBitSet>(TConstArrayView<FMassFragmentRequirementDescription> Requirements
		, TMassExecutionAccess<FMassConstSharedFragmentBitSet>& Out)
	{
		for (const FMassFragmentRequirementDescription& Requirement : Requirements)
		{
			if (Requirement.Presence != EMassFragmentPresence::None)
			{
				check(Requirement.StructType);
				if (ensureMsgf(Requirement.AccessMode == EMassFragmentAccess::ReadOnly, TEXT("ReadOnly is the only supported AccessMode for ConstSharedFragments")))
				{
					Out.Read.Add(*Requirement.StructType);
				}
			}
		}
	}
}

//////////////////////////////////////////////////////////////////////
// FMassSubsystemRequirements

void FMassSubsystemRequirements::ExportRequirements(FMassExecutionRequirements& OutRequirements) const
{
	OutRequirements.RequiredSubsystems.Read += RequiredConstSubsystems;
	OutRequirements.RequiredSubsystems.Write += RequiredMutableSubsystems;
}

void FMassSubsystemRequirements::Reset()
{
	RequiredConstSubsystems.Reset();
	RequiredMutableSubsystems.Reset();
	bRequiresGameThreadExecution = false;
}

//////////////////////////////////////////////////////////////////////
// FMassFragmentRequirements

FMassFragmentRequirements::FMassFragmentRequirements(std::initializer_list<UScriptStruct*> InitList)
{
	for (const UScriptStruct* FragmentType : InitList)
	{
		AddRequirement(FragmentType, EMassFragmentAccess::ReadWrite, EMassFragmentPresence::All);
	}
}

FMassFragmentRequirements::FMassFragmentRequirements(TConstArrayView<const UScriptStruct*> InitList)
{
	for (const UScriptStruct* FragmentType : InitList)
	{
		AddRequirement(FragmentType, EMassFragmentAccess::ReadWrite, EMassFragmentPresence::All);
	}
}

FMassFragmentRequirements& FMassFragmentRequirements::ClearTagRequirements(const FMassTagBitSet& TagsToRemoveBitSet)
{
	RequiredAllTags.Remove(TagsToRemoveBitSet);
	RequiredAnyTags.Remove(TagsToRemoveBitSet);
	RequiredNoneTags.Remove(TagsToRemoveBitSet);
	RequiredOptionalTags.Remove(TagsToRemoveBitSet);

	return *this;
}

void FMassFragmentRequirements::SortRequirements()
{
	// we're sorting the Requirements the same way ArchetypeData's FragmentConfig is sorted (see FMassArchetypeData::Initialize)
	// so that when we access ArchetypeData.FragmentConfigs in FMassArchetypeData::BindRequirementsWithMapping
	// (via GetFragmentData call) the access is sequential (i.e. not random) and there's a higher chance the memory
	// FragmentConfigs we want to access have already been fetched and are available in processor cache.
	FragmentRequirements.Sort(FScriptStructSortOperator());
	ChunkFragmentRequirements.Sort(FScriptStructSortOperator());
	ConstSharedFragmentRequirements.Sort(FScriptStructSortOperator());
	SharedFragmentRequirements.Sort(FScriptStructSortOperator());
}

FORCEINLINE void FMassFragmentRequirements::CachePropreties() const
{
	if (bPropertiesCached == false)
	{
		bHasPositiveRequirements = !(RequiredAllTags.IsEmpty()
			&& RequiredAnyTags.IsEmpty()
			&& RequiredAllFragments.IsEmpty()
			&& RequiredAnyFragments.IsEmpty()
			&& RequiredAllChunkFragments.IsEmpty()
			&& RequiredAllSharedFragments.IsEmpty()
			&& RequiredAllConstSharedFragments.IsEmpty());

		bHasNegativeRequirements = !(RequiredNoneTags.IsEmpty()
			&& RequiredNoneFragments.IsEmpty()
			&& RequiredNoneChunkFragments.IsEmpty()
			&& RequiredNoneSharedFragments.IsEmpty()
			&& RequiredNoneConstSharedFragments.IsEmpty());

		bHasOptionalRequirements = !(RequiredOptionalFragments.IsEmpty()
				&& RequiredOptionalTags.IsEmpty()
				&& RequiredOptionalChunkFragments.IsEmpty()
				&& RequiredOptionalSharedFragments.IsEmpty()
				&& RequiredOptionalConstSharedFragments.IsEmpty());

		bPropertiesCached = true;
	}
}

bool FMassFragmentRequirements::CheckValidity() const
{
	CachePropreties();
	// @todo we need to add more sophisticated testing somewhere to detect contradicting requirements - like having and not having a given tag.
	return bHasPositiveRequirements || bHasNegativeRequirements || bHasOptionalRequirements;
}

bool FMassFragmentRequirements::IsEmpty() const
{
	CachePropreties();
	// note that even though at the moment the following condition is the same as negation of current CheckValidity value
	// that will change in the future (with additional validity checks).
	return !bHasPositiveRequirements && !bHasNegativeRequirements && !bHasOptionalRequirements;
}

bool FMassFragmentRequirements::DoesMatchAnyOptionals(const FMassArchetypeCompositionDescriptor& ArchetypeComposition) const
{
	return bHasOptionalRequirements
		&& (ArchetypeComposition.Fragments.HasAny(RequiredOptionalFragments)
			|| ArchetypeComposition.Tags.HasAny(RequiredOptionalTags)
			|| ArchetypeComposition.ChunkFragments.HasAny(RequiredOptionalChunkFragments)
			|| ArchetypeComposition.SharedFragments.HasAny(RequiredOptionalSharedFragments)
			|| ArchetypeComposition.ConstSharedFragments.HasAny(RequiredOptionalConstSharedFragments));
}

bool FMassFragmentRequirements::DoesArchetypeMatchRequirements(const FMassArchetypeHandle& ArchetypeHandle) const
{
	check(ArchetypeHandle.IsValid());
	const FMassArchetypeData* Archetype = FMassArchetypeHelper::ArchetypeDataFromHandle(ArchetypeHandle);
	CA_ASSUME(Archetype);

	return DoesArchetypeMatchRequirements(Archetype->GetCompositionDescriptor());
}
	
bool FMassFragmentRequirements::DoesArchetypeMatchRequirements(const FMassArchetypeCompositionDescriptor& ArchetypeComposition) const
{
	CachePropreties();

	const bool bPassNegativeFilter = bHasNegativeRequirements == false
		|| (ArchetypeComposition.Fragments.HasNone(RequiredNoneFragments)
			&& ArchetypeComposition.Tags.HasNone(RequiredNoneTags)
			&& ArchetypeComposition.ChunkFragments.HasNone(RequiredNoneChunkFragments)
			&& ArchetypeComposition.SharedFragments.HasNone(RequiredNoneSharedFragments)
			&& ArchetypeComposition.ConstSharedFragments.HasNone(RequiredNoneConstSharedFragments));
	
	if (bPassNegativeFilter)
	{
		if (bHasPositiveRequirements)
		{
			return ArchetypeComposition.Fragments.HasAll(RequiredAllFragments)
				&& (RequiredAnyFragments.IsEmpty() || ArchetypeComposition.Fragments.HasAny(RequiredAnyFragments))
				&& ArchetypeComposition.Tags.HasAll(RequiredAllTags)
				&& (RequiredAnyTags.IsEmpty() || ArchetypeComposition.Tags.HasAny(RequiredAnyTags))
				&& ArchetypeComposition.ChunkFragments.HasAll(RequiredAllChunkFragments)
				&& ArchetypeComposition.SharedFragments.HasAll(RequiredAllSharedFragments)
				&& ArchetypeComposition.ConstSharedFragments.HasAll(RequiredAllConstSharedFragments);
		}
		else if (bHasOptionalRequirements)
		{
			return DoesMatchAnyOptionals(ArchetypeComposition);
		}
		// else - it's fine, we passed all the filters that have been set up
		return true;
	}
	return false;
}

void FMassFragmentRequirements::ExportRequirements(FMassExecutionRequirements& OutRequirements) const
{
	using UE::Mass::Private::ExportRequirements;
	ExportRequirements<FMassFragmentBitSet>(FragmentRequirements, OutRequirements.Fragments);
	ExportRequirements<FMassChunkFragmentBitSet>(ChunkFragmentRequirements, OutRequirements.ChunkFragments);
	ExportRequirements<FMassSharedFragmentBitSet>(SharedFragmentRequirements, OutRequirements.SharedFragments);
	ExportRequirements<FMassConstSharedFragmentBitSet>(ConstSharedFragmentRequirements, OutRequirements.ConstSharedFragments);

	OutRequirements.RequiredAllTags = RequiredAllTags;
	OutRequirements.RequiredAnyTags = RequiredAnyTags;
	OutRequirements.RequiredNoneTags = RequiredNoneTags;
	// not exporting optional tags by design
}

void FMassFragmentRequirements::Reset()
{
	FragmentRequirements.Reset();
	ChunkFragmentRequirements.Reset();
	ConstSharedFragmentRequirements.Reset();
	SharedFragmentRequirements.Reset();
	RequiredAllTags.Reset();
	RequiredAnyTags.Reset();
	RequiredNoneTags.Reset();
	RequiredAllFragments.Reset();
	RequiredAnyFragments.Reset();
	RequiredOptionalFragments.Reset();
	RequiredNoneFragments.Reset();
	RequiredAllChunkFragments.Reset();
	RequiredOptionalChunkFragments.Reset();
	RequiredNoneChunkFragments.Reset();
	RequiredAllSharedFragments.Reset();
	RequiredOptionalSharedFragments.Reset();
	RequiredNoneSharedFragments.Reset();
	RequiredAllConstSharedFragments.Reset();
	RequiredOptionalConstSharedFragments.Reset();
	RequiredNoneConstSharedFragments.Reset();

	IncrementalChangesCount = 0;
}
