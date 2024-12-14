// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Interfaces/TypedElementDataStorageFactory.h"
#include "OverrideFactory.generated.h"

UCLASS()
class UHierarchyTableOverrideFactory : public UEditorDataStorageFactory
{
	GENERATED_BODY()

public:
	void RegisterWidgetConstructors(IEditorDataStorageProvider& DataStorage, IEditorDataStorageUiProvider& DataStorageUi) const override;
};