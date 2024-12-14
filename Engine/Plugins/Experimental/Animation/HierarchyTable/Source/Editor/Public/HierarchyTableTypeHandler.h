// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "StructUtils/InstancedStruct.h"
#include "HierarchyTableTypeHandler.generated.h"

class UHierarchyTable;
class UScriptStruct;

UCLASS(Abstract)
class HIERARCHYTABLEEDITOR_API UHierarchyTableTypeHandler_Base : public UObject
{
	GENERATED_BODY()

public:
	virtual FInstancedStruct GetDefaultEntry() const { return FInstancedStruct(); }

	virtual void InitializeTable(TObjectPtr<UHierarchyTable> InHierarchyTable) const {};

	virtual TArray<UScriptStruct*> GetColumns() const { return {}; };
};