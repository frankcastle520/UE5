// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Interfaces/TypedElementDataStorageFactory.h"
#include "UObject/ObjectMacros.h"

#include "TypedElementActorIconOverrideQueries.generated.h"

UCLASS()
class UActorIconOverrideDataStorageFactory : public UEditorDataStorageFactory
{
	GENERATED_BODY()

public:
	virtual ~UActorIconOverrideDataStorageFactory() override = default;

	virtual void RegisterQueries(IEditorDataStorageProvider& DataStorage) override;
};
