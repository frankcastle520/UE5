// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "HierarchyTableType.h"
#include "Templates/SubclassOf.h"
#include "Templates/SharedPointer.h"
#include "StructUtils/InstancedStruct.h"

#include "HierarchyTable.generated.h"

class USkeleton;

UENUM()
enum class EHierarchyTableEntryType : uint8
{
	Bone,
	Curve,
	Attribute
};

class UHierarchyTable;

USTRUCT()
struct HIERARCHYTABLERUNTIME_API FHierarchyTableEntryData
{
	GENERATED_BODY()

public:
	FHierarchyTableEntryData()
		: EntryType(EHierarchyTableEntryType::Bone)
		, Parent(INDEX_NONE)
	{
	}

	friend UHierarchyTable;

	UPROPERTY()
	TObjectPtr<UHierarchyTable> OwnerTable;

	UPROPERTY()
	EHierarchyTableEntryType EntryType;

	UPROPERTY()
	FName Identifier;

	UPROPERTY()
	int32 Parent;

	UPROPERTY()
	TOptional<FInstancedStruct> Payload;

public:
	bool HasParent() const { return Parent != INDEX_NONE; }
	bool IsOverridden() const { return Payload.IsSet(); }
	bool HasOverriddenChildren() const;

	void ToggleOverridden();

	template <typename T>
	const T* GetValue() const
	{
		const FInstancedStruct* StructPtr = GetActualValue();
		check(StructPtr->IsValid());
		return StructPtr->GetPtr<T>();
	}

	template <typename T>
	T* GetMutableValue()
	{
		check(IsOverridden());
		return Payload.GetValue().GetMutablePtr<T>();
	}

	const FHierarchyTableEntryData* GetClosestAncestor();

private:
	// TODO: This needs to be cached in some way as it potentially goes up the entire hierarchy until it finds an ancestor with an overridden value
	// This is called each time a widget is ticked so grows exponentially with the height of the hierarchy. This needs addressing at some point
	// but cached value must be updated when any of its direct ancestors' value is updated.
	const FInstancedStruct* GetActualValue() const;

	const FInstancedStruct* GetFromClosestAncestor() const;

	bool IsOverriddenOrHasOverriddenChildren(const bool bIncludeSelf) const;
};

UCLASS()
class HIERARCHYTABLERUNTIME_API UHierarchyTable : public UObject
{
	GENERATED_BODY()

public:
	UHierarchyTable();

	UPROPERTY(VisibleAnywhere, Category=Default)
	TObjectPtr<USkeleton> Skeleton;

	UPROPERTY(VisibleAnywhere, Category = Default)
	TObjectPtr<const UScriptStruct> TableType;

	UPROPERTY()
	TArray<FHierarchyTableEntryData> TableData;

	void InitializeTable(FInstancedStruct DefaultEntry);

	TArray<const FHierarchyTableEntryData*> GetChildren(const FHierarchyTableEntryData& Parent) const;

	bool HasIdentifier(const FName Identifier) const;

	FHierarchyTableEntryData* FindEntry(const FName EntryIdentifier, const EHierarchyTableEntryType EntryType);
};
