// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Common/TypedElementHandles.h"
#include "Elements/Interfaces/TypedElementDataStorageFactory.h"

#include "ObjectPackagePathToColumnQueries.generated.h"

class IEditorDataStorageProvider;

UCLASS()
class UTypedElementUObjectPackagePathFactory : public UEditorDataStorageFactory
{
	GENERATED_BODY()

public:
	~UTypedElementUObjectPackagePathFactory() override = default;

	void RegisterQueries(IEditorDataStorageProvider& DataStorage) override;

private:
	void RegisterTryAddPackageRef(IEditorDataStorageProvider& DataStorage);
	UE::Editor::DataStorage::QueryHandle TryAddPackageRef = UE::Editor::DataStorage::InvalidQueryHandle;
};
