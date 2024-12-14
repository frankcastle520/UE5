// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Common/TypedElementCommonTypes.h"
#include "UObject/WeakObjectPtrTemplates.h"

#include "TypedElementTypeInfoColumns.generated.h"

class UClass;
class UScriptStruct;

/**
 * Column that stores type information for classes.
 */
USTRUCT(meta = (DisplayName = "Type"))
struct FTypedElementClassTypeInfoColumn final : public FEditorDataStorageColumn
{
	GENERATED_BODY()

	TWeakObjectPtr<const UClass> TypeInfo;
};

/**
 * Column that stores type information for structs.
 */
USTRUCT(meta = (DisplayName = "ScriptStruct type info"))
struct FTypedElementScriptStructTypeInfoColumn final : public FEditorDataStorageColumn
{
	GENERATED_BODY()

	TWeakObjectPtr<const UScriptStruct> TypeInfo;
};
