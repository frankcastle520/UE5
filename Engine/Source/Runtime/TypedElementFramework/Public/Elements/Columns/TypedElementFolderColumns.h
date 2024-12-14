// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Interfaces/TypedElementDataStorageInterface.h"

#include "TypedElementFolderColumns.generated.h"

/**
 * Column that signifies this row is for a folder
 */
USTRUCT(meta = (DisplayName = "Folder"))
struct FFolderTag final : public FEditorDataStorageTag
{
	GENERATED_BODY()
};