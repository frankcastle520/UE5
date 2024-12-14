// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Interfaces/TypedElementDataStorageFactory.h"
#include "TimeProfileFactory.generated.h"

UCLASS()
class UHierarchyTableTimeFactory : public UEditorDataStorageFactory
{
	GENERATED_BODY()

public:
	void RegisterWidgetConstructors(IEditorDataStorageProvider& DataStorage, IEditorDataStorageUiProvider& DataStorageUi) const override;
};