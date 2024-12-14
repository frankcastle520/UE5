// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Common/TypedElementCommonTypes.h"

#include "TimeProfileProxyColumn.generated.h"

USTRUCT()
struct FHierarchyTableTimeColumn_StartTime final : public FEditorDataStorageColumn
{
	GENERATED_BODY()
};

USTRUCT()
struct FHierarchyTableTimeColumn_EndTime final : public FEditorDataStorageColumn
{
	GENERATED_BODY()
};

USTRUCT()
struct FHierarchyTableTimeColumn_TimeFactor final : public FEditorDataStorageColumn
{
	GENERATED_BODY()
};

USTRUCT()
struct FHierarchyTableTimeColumn_Preview final : public FEditorDataStorageColumn
{
	GENERATED_BODY()
};