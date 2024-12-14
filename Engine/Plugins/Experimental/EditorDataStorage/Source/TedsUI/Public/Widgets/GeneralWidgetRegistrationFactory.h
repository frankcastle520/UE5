// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Interfaces/TypedElementDataStorageFactory.h"
#include "UObject/ObjectMacros.h"

#include "GeneralWidgetRegistrationFactory.generated.h"

UCLASS()
class TEDSUI_API UGeneralWidgetRegistrationFactory : public UEditorDataStorageFactory
{
	GENERATED_BODY()

public:
	static const FName CellPurpose;
	static const FName LargeCellPurpose;
	static const FName HeaderPurpose;
	static const FName CellDefaultPurpose;
	static const FName HeaderDefaultPurpose;

	~UGeneralWidgetRegistrationFactory() override = default;

	void RegisterWidgetPurposes(IEditorDataStorageUiProvider& DataStorageUi) const override;
};
