// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Common/TypedElementCommonTypes.h"

#include "HierarchyTableProxyColumn.generated.h"

class UHierarchyTable;
struct FHierarchyTableEntryData;

USTRUCT()
struct FHierarchyTableProxyColumn : public FEditorDataStorageColumn
{
	GENERATED_BODY()

public:
	UHierarchyTable* OwnerTable;

	int32 OwnerEntryIndex;
};