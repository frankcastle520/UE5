// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UniversalObjectLocator.h"
#include "UniversalObjectLocatorStringParams.h"
#include "Variables/AnimNextVariableBindingData.h"
#include "AnimNextUniversalObjectLocatorBindingData.generated.h"

// Type of binding
UENUM()
enum class FAnimNextUniversalObjectLocatorBindingType : uint8
{
	Property,
	Function,
	HoistedFunction,
};

// Allows binding of module variables to gameplay data via Universal Object Locators
USTRUCT(DisplayName = "Universal Object Locator")
struct FAnimNextUniversalObjectLocatorBindingData : public FAnimNextVariableBindingData
{
	GENERATED_BODY()

	// Property to use (if a property)
	UPROPERTY(EditAnywhere, Category = "Binding")
	TFieldPath<FProperty> Property;

	// Function to use (if a function)
	UPROPERTY(EditAnywhere, Category = "Binding")
	TSoftObjectPtr<UFunction> Function;

	// Object locator
	UPROPERTY(EditAnywhere, Category = "Binding", meta = (LocatorContext="AnimNextContext"))
	FUniversalObjectLocator Locator;

	UPROPERTY(EditAnywhere, Category = "Binding")
	FAnimNextUniversalObjectLocatorBindingType Type = FAnimNextUniversalObjectLocatorBindingType::Property;

	// FAnimNextVariableBindingData interface
	virtual bool IsValid() const override
	{
		const bool bValidLocator = !Locator.IsEmpty();
		const bool bValidProperty = (Type == FAnimNextUniversalObjectLocatorBindingType::Property && !Property.IsPathToFieldEmpty());
		const bool bValidFunction = ((Type == FAnimNextUniversalObjectLocatorBindingType::Function || Type == FAnimNextUniversalObjectLocatorBindingType::HoistedFunction) && !Function.IsNull());
		return bValidLocator && (bValidProperty || bValidFunction);
	}
};
