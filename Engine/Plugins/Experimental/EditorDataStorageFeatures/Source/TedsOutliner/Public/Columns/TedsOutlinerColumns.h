// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "Templates/SharedPointer.h"

#include "TedsOutlinerColumns.generated.h"

class ISceneOutliner;

// Column used to store a reference to the Teds Outliner owning a specific row
// Currently only added to widget rows in the Teds Outliner
USTRUCT(meta = (DisplayName = "Owning Table Viewer"))
struct FTedsOutlinerColumn final : public FEditorDataStorageColumn
{
	GENERATED_BODY()

	TWeakPtr<ISceneOutliner> Outliner;
};
