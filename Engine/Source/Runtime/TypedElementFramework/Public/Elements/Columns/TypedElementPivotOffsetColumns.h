// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Common/TypedElementCommonTypes.h"

#include "TypedElementPivotOffsetColumns.generated.h"

USTRUCT()
struct FTypedElementPivotOffset final : public FEditorDataStorageColumn
{
	GENERATED_BODY()

	UPROPERTY(meta = (IgnoreForMemberInitializationTest))
	FVector Offset;
};