// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Common/TypedElementHandles.h"
#include "Elements/Interfaces/TypedElementDataStorageFactory.h"
#include "UObject/ObjectMacros.h"

#include "InstanceDataObjectFixupToolTedsQueries.generated.h"

UCLASS()
class UInstanceDataObjectFixupToolTedsQueryFactory : public UEditorDataStorageFactory
{
	GENERATED_BODY()

public:
	~UInstanceDataObjectFixupToolTedsQueryFactory() override = default;

	void RegisterQueries(IEditorDataStorageProvider& DataStorage) override;

private:
	static void ShowFixUpToolForPlaceholders(UE::Editor::DataStorage::RowHandle Row);
	static void ShowFixUpToolForLooseProperties(UE::Editor::DataStorage::RowHandle Row);
	static void ShowFixUpTool(UE::Editor::DataStorage::RowHandle Row, bool bRecurseIntoObject);
};
