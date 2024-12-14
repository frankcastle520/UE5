// Copyright Epic Games, Inc. All Rights Reserved.

#include "HierarchyTable.h"
#include "Animation/Skeleton.h"
#include "ReferenceSkeleton.h"

UHierarchyTable::UHierarchyTable()
{}

void UHierarchyTable::InitializeTable(FInstancedStruct DefaultEntry)
{
	check(Skeleton);
	check(TableData.Num() == 0);

	FReferenceSkeleton RefSkeleton = Skeleton->GetReferenceSkeleton();

	for (int32 BoneIndex = 0; BoneIndex < RefSkeleton.GetNum(); ++BoneIndex)
	{
		FHierarchyTableEntryData EntryData;
		EntryData.Parent = RefSkeleton.GetParentIndex(BoneIndex);
		EntryData.Identifier = RefSkeleton.GetBoneName(BoneIndex);
		EntryData.EntryType = EHierarchyTableEntryType::Bone;
		EntryData.Payload = BoneIndex == 0 ? DefaultEntry : TOptional<FInstancedStruct>();
		EntryData.OwnerTable = this;

		TableData.Add(EntryData);
	}
}

bool FHierarchyTableEntryData::HasOverriddenChildren() const
{
	return IsOverriddenOrHasOverriddenChildren(false);
}

bool FHierarchyTableEntryData::IsOverriddenOrHasOverriddenChildren(const bool bIncludeSelf) const
{
	if (bIncludeSelf && IsOverridden())
	{
		return true;
	}

	TArray<const FHierarchyTableEntryData*> Children = OwnerTable->GetChildren(*this);
	for (const FHierarchyTableEntryData* Child : Children)
	{
		if (Child->IsOverriddenOrHasOverriddenChildren(true))
		{
			return true;
		}
	}

	return false;
}

void FHierarchyTableEntryData::ToggleOverridden()
{
	if (Payload.IsSet())
	{
		Payload.Reset();
	}
	else
	{
		Payload = *GetFromClosestAncestor();
	}
}

const FInstancedStruct* FHierarchyTableEntryData::GetActualValue() const
{
	return IsOverridden() ? Payload.GetPtrOrNull() : GetFromClosestAncestor();
}

const FHierarchyTableEntryData* FHierarchyTableEntryData::GetClosestAncestor()
{
	return IsOverridden() ? this : OwnerTable->TableData[Parent].GetClosestAncestor();
}

const FInstancedStruct* FHierarchyTableEntryData::GetFromClosestAncestor() const
{
	check(Parent != INDEX_NONE);

	const FHierarchyTableEntryData& ParentEntry = OwnerTable->TableData[Parent];
	return ParentEntry.GetActualValue();
}

TArray<const FHierarchyTableEntryData*> UHierarchyTable::GetChildren(const FHierarchyTableEntryData& Parent) const
{
	int32 ParentIndex = TableData.IndexOfByPredicate([Parent](const FHierarchyTableEntryData& Candidate)
		{
			return Candidate.Identifier == Parent.Identifier;
		});

	if (ParentIndex == INDEX_NONE)
	{
		return TArray<const FHierarchyTableEntryData*>();
	}

	TArray<const FHierarchyTableEntryData*> Children;

	for (const FHierarchyTableEntryData& Entry : TableData)
	{
		if (Entry.Parent == ParentIndex)
		{
			Children.Add(&Entry);
		}
	}

	return Children;
}

bool UHierarchyTable::HasIdentifier(const FName Identifier) const
{
	return TableData.ContainsByPredicate([Identifier](const FHierarchyTableEntryData& Entry)
		{
			return Entry.Identifier == Identifier;
		});
}

FHierarchyTableEntryData* UHierarchyTable::FindEntry(const FName EntryIdentifier, const EHierarchyTableEntryType EntryType)
{
	return TableData.FindByPredicate([EntryIdentifier, EntryType](const FHierarchyTableEntryData& Entry)
		{
			return Entry.Identifier == EntryIdentifier && Entry.EntryType == EntryType;
		});
}