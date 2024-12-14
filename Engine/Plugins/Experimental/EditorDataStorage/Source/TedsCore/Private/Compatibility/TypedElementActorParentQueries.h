// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Interfaces/TypedElementDataStorageFactory.h"
#include "UObject/ObjectMacros.h"

#include "TypedElementActorParentQueries.generated.h"

UCLASS()
class UActorParentDataStorageFactory : public UEditorDataStorageFactory
{
	GENERATED_BODY()

public:
	~UActorParentDataStorageFactory() override = default;

	void RegisterQueries(IEditorDataStorageProvider& DataStorage) override;

private:
	/**
	 * Checks rows with actors that don't have a parent column yet if one needs to be added whenever
	 * the row is marked for updates.
	 */
	void RegisterAddParentColumn(IEditorDataStorageProvider& DataStorage) const;
	/**
	 * Updates the parent column with the parent from the actor or removes it if there's no parent associated
	 * with the actor anymore.
	 */
	void RegisterUpdateOrRemoveParentColumn(IEditorDataStorageProvider& DataStorage) const;
};
