// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Interfaces/TypedElementDataStorageFactory.h"
#include "UObject/ObjectMacros.h"

#include "TypedElementMiscQueries.generated.h"

/**
 * Removes all FTypedElementSyncBackToWorldTags at the end of an update cycle.
 */
UCLASS()
class UTypedElementRemoveSyncToWorldTagFactory : public UEditorDataStorageFactory
{
	GENERATED_BODY()

public:
	~UTypedElementRemoveSyncToWorldTagFactory() override = default;

	void RegisterQueries(IEditorDataStorageProvider& DataStorage) override;
};
