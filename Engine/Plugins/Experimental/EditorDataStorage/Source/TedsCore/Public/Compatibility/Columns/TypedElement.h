// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Common/TypedElementCommonTypes.h"
#include "Elements/Framework/TypedElementHandle.h"

#include "TypedElement.generated.h"

// A column which contains a TypedElementHandle from the TypedElementFramework
// Prefer to use the namespaced alias when referencing in code - UE::Editor::DataStorage::Compatibility::FTypedElementColumn
USTRUCT()
struct FTedsTypedElementColumn : public FEditorDataStorageColumn
{
	GENERATED_BODY()

	FTypedElementHandle Handle;
};

namespace UE::Editor::DataStorage::Compatibility
{
	using FTypedElementColumn = FTedsTypedElementColumn;
}  // namespace UE::Editor::DataStorage::Compatibility
