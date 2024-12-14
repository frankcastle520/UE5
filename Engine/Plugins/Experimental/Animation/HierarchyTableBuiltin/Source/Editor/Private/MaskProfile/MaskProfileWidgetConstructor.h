// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Interfaces/TypedElementDataStorageUiInterface.h"
#include "HierarchyTableWidgetConstructor.h"

#include "MaskProfileWidgetConstructor.generated.h"

USTRUCT()
struct FHierarchyTableMaskWidgetConstructor_Value : public FHierarchyTableWidgetConstructor
{
	GENERATED_BODY()

public:
	FHierarchyTableMaskWidgetConstructor_Value() : Super(StaticStruct()) {}
	~FHierarchyTableMaskWidgetConstructor_Value() override = default;

protected:
	virtual TSharedRef<SWidget> CreateInternalWidget(UHierarchyTable* HierarchyTable, int32 EntryIndex) override;
};