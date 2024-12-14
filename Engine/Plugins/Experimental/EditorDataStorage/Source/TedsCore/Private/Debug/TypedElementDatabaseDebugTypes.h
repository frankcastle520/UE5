// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Common/TypedElementCommonTypes.h"
#include "Elements/Interfaces/TypedElementDataStorageFactory.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/ObjectMacros.h"

#include "TypedElementDatabaseDebugTypes.generated.h"

UENUM()
enum class ETedsDebugEnum : int8
{
	Red,
	Blue,
	Green,
	Yellow,
	Black,
	Pink,
	Orange,
	Purple
};

USTRUCT()
struct FTestDynamicColumn : public FEditorDataStorageColumn
{
	GENERATED_BODY()

	// Note: Not trivial type to check that non-default move/copying works
	// Should work regardless of UPROPERTY
	TArray<int32> IntArray;
};

USTRUCT()
struct FTestDynamicTag : public FEditorDataStorageTag
{
	GENERATED_BODY()
};
