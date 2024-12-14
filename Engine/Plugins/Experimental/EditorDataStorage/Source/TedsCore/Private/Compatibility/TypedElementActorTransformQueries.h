// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Interfaces/TypedElementDataStorageFactory.h"
#include "UObject/ObjectMacros.h"

#include "TypedElementActorTransformQueries.generated.h"

UCLASS()
class UActorTransformDataStorageFactory : public UEditorDataStorageFactory
{
	GENERATED_BODY()

public:
	~UActorTransformDataStorageFactory() override = default;

	void RegisterQueries(IEditorDataStorageProvider& DataStorage) override;

private:
	/**
	 * Checks actors that don't have a transform column and adds one if an actor has been
	 * assigned a transform.
	 */
	void RegisterActorAddTransformColumn(IEditorDataStorageProvider& DataStorage) const;
	/**
	 * Takes the transform set on an actor and copies it to the Data Storage or removes the
	 * transform column if there's not transform available anymore.
	 */
	void RegisterActorLocalTransformToColumn(IEditorDataStorageProvider& DataStorage) const;
	/**
	 * Takes the transform stored in the Data Storage and copies it to the actor's tranform if
	 * the FTypedElementSyncBackToWorldTag has been set.
	 */
	void RegisterLocalTransformColumnToActor(IEditorDataStorageProvider& DataStorage) const;
};
