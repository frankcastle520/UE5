// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HierarchyTableTypeHandler.h"

#include "MaskProfileTypeHandler.generated.h"

UCLASS()
class UHierarchyTableTypeHandler_Mask final : public UHierarchyTableTypeHandler_Base
{
	GENERATED_BODY()

public:
	FInstancedStruct GetDefaultEntry() const override;

	TArray<UScriptStruct*> GetColumns() const override;
};