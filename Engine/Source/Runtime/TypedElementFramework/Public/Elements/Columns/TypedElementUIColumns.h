// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Common/TypedElementCommonTypes.h"
#include "UObject/ObjectMacros.h"

#include "TypedElementUIColumns.generated.h"

/**
 * Tag to indicate that this row should not show up in any TEDS UI that displays rows (e.g table viewer)
 */
USTRUCT(meta = (DisplayName = "Hide row from UI"))
struct FHideRowFromUITag final : public FEditorDataStorageTag
{
	GENERATED_BODY()
};