// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HierarchyTableProxyColumn.h"

#include "OverrideColumn.generated.h"

USTRUCT(meta = (DisplayName = "Override"))
struct FTypedElementOverrideColumn final : public FHierarchyTableProxyColumn
{
	GENERATED_BODY()
};