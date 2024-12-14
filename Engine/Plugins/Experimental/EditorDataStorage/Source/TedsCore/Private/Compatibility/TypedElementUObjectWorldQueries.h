// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Interfaces/TypedElementDataStorageFactory.h"
#include "UObject/ObjectMacros.h"

#include "TypedElementUObjectWorldQueries.generated.h"

UCLASS()
class UObjectWorldDataStorageFactory : public UEditorDataStorageFactory
{
	GENERATED_BODY()

public:
	~UObjectWorldDataStorageFactory() override = default;

	void RegisterQueries(IEditorDataStorageProvider& DataStorage) override;

private:
	/**
	 * Checks rows with UObjects that don't have a world column yet if one needs to be added whenever
	 * the row is marked for updates.
	 */
	void RegisterAddWorldColumn(IEditorDataStorageProvider& DataStorage) const;
	/**
	 * Updates the world column with the world in the UObject or removes it if there's no world associated
	 * with the UObject anymore.
	 */
	void RegisterUpdateOrRemoveWorldColumn(IEditorDataStorageProvider& DataStorage) const;
};
