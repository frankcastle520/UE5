// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCOE/Nodes/CustomizableObjectNodeVariation.h"

#include "CustomizableObjectNodeComponentVariation.generated.h"

struct FCustomizableObjectComponentVariation;


UENUM(BlueprintType)
enum class ECustomizableObjectNodeComponentVariationType : uint8
{
	Tag 		UMETA(DisplayName = "Tag"),
	State 		UMETA(DisplayName = "State"),
};


UCLASS()
class CUSTOMIZABLEOBJECTEDITOR_API UCustomizableObjectNodeComponentVariation : public UCustomizableObjectNodeVariation
{
public:
	GENERATED_BODY()

	//UPROPERTY(EditAnywhere, Category = CustomizableObject)
	//ECustomizableObjectNodeComponentVariationType Type = ECustomizableObjectNodeComponentVariationType::Tag;

public:
	// UCustomizableObjectNode interface
	virtual bool IsSingleOutputNode() const override;
	
	// UCustomizableObjectNodeVariation interface
	virtual FName GetCategory() const override;
	virtual bool IsInputPinArray() const override;
};

