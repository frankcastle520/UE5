// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Interfaces/TypedElementDataStorageFactory.h"
#include "UObject/ObjectMacros.h"

#include "TypedElementActorLabelQueries.generated.h"


UCLASS()
class UActorLabelDataStorageFactory : public UEditorDataStorageFactory
{
	GENERATED_BODY()

public:
	~UActorLabelDataStorageFactory() override = default;

	void RegisterQueries(IEditorDataStorageProvider& DataStorage) override;

private:
	/**
	 * Takes the label set on an actor and copies it to the Data Storage if they differ.
	 */
	void RegisterActorLabelToColumnQuery(IEditorDataStorageProvider& DataStorage) const;
	/**
	 * Takes the label stored in the Data Storage and copies it to the actor's label if the FTypedElementSyncBackToWorldTag
	 * has been set and the labels differ.
	 */
	void RegisterLabelColumnToActorQuery(IEditorDataStorageProvider& DataStorage) const;
};