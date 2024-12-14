// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Interfaces/TypedElementDataStorageUiInterface.h"
#include "HierarchyTableWidgetConstructor.h"

#include "TimeProfileWidgetConstructor.generated.h"

USTRUCT()
struct FHierarchyTableTimeWidgetConstructor_StartTime : public FHierarchyTableWidgetConstructor
{
	GENERATED_BODY()

public:
	FHierarchyTableTimeWidgetConstructor_StartTime() : Super(StaticStruct()) {}
	~FHierarchyTableTimeWidgetConstructor_StartTime() override = default;

protected:
	virtual TSharedRef<SWidget> CreateInternalWidget(UHierarchyTable* HierarchyTable, int32 EntryIndex) override;
};

USTRUCT()
struct FHierarchyTableTimeWidgetConstructor_EndTime : public FHierarchyTableWidgetConstructor
{
	GENERATED_BODY()

public:
	FHierarchyTableTimeWidgetConstructor_EndTime() : Super(StaticStruct()) {}
	~FHierarchyTableTimeWidgetConstructor_EndTime() override = default;

protected:
	virtual TSharedRef<SWidget> CreateInternalWidget(UHierarchyTable* HierarchyTable, int32 EntryIndex) override;
};

USTRUCT()
struct FHierarchyTableTimeWidgetConstructor_TimeFactor : public FHierarchyTableWidgetConstructor
{
	GENERATED_BODY()

public:
	FHierarchyTableTimeWidgetConstructor_TimeFactor() : Super(StaticStruct()) {}
	~FHierarchyTableTimeWidgetConstructor_TimeFactor() override = default;

protected:
	virtual TSharedRef<SWidget> CreateInternalWidget(UHierarchyTable* HierarchyTable, int32 EntryIndex) override;
};

USTRUCT()
struct FHierarchyTableTimeWidgetConstructor_Preview : public FHierarchyTableWidgetConstructor
{
	GENERATED_BODY()

public:
	FHierarchyTableTimeWidgetConstructor_Preview() : Super(StaticStruct()) {}
	~FHierarchyTableTimeWidgetConstructor_Preview() override = default;

protected:
	virtual TSharedRef<SWidget> CreateInternalWidget(UHierarchyTable* HierarchyTable, int32 EntryIndex) override;
};