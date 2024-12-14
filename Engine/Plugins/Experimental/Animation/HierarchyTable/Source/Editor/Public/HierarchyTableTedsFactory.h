// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Interfaces/TypedElementDataStorageFactory.h"
#include "Elements/Interfaces/TypedElementQueryStorageInterfaces.h"
#include "UObject/ObjectMacros.h"

#include "HierarchyTableTedsFactory.generated.h"

UCLASS()
class UTypedElementHierarchyTableFactory : public UEditorDataStorageFactory
{
	GENERATED_BODY()

public:
	~UTypedElementHierarchyTableFactory() override = default;

	void RegisterTables(IEditorDataStorageProvider& DataStorage) override;
	void RegisterQueries(IEditorDataStorageProvider& DataStorage) override;
};
