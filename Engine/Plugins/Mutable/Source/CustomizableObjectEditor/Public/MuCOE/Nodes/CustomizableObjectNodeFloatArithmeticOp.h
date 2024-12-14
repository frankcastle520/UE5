// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCOE/Nodes/CustomizableObjectNode.h"

#include "CustomizableObjectNodeFloatArithmeticOp.generated.h"

namespace ENodeTitleType { enum Type : int; }

class UCustomizableObjectNodeRemapPins;
class UEdGraphPin;
class UObject;
struct FPropertyChangedEvent;


UENUM(BlueprintType)
enum class EFloatArithmeticOperation : uint8
{
	E_Add 	UMETA(DisplayName = "+"),
	E_Sub 	UMETA(DisplayName = "-"),
	E_Mul	UMETA(DisplayName = "x"),
	E_Div	UMETA(DisplayName = "/")
};

UCLASS()
class CUSTOMIZABLEOBJECTEDITOR_API UCustomizableObjectNodeFloatArithmeticOp : public UCustomizableObjectNode
{
public:
	GENERATED_BODY()

	UCustomizableObjectNodeFloatArithmeticOp();

	// Begin EdGraphNode interface
	FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	FLinearColor GetNodeTitleColor() const override;
	FText GetTooltipText() const override;
	bool ShouldOverridePinNames() const override { return true; }

	// UCustomizableObjectNode interface
	void AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins) override;

	// First Operand
	UEdGraphPin* XPin() const
	{
		return FindPin(TEXT("A"));
	}

	// Second Operand
	UEdGraphPin* YPin() const
	{
		return FindPin(TEXT("B"));
	}

	UPROPERTY(EditAnywhere, Category = FloatArithmeticOperation)
	EFloatArithmeticOperation Operation;

	// UCustomizableObjectNode interface
	virtual bool IsAffectedByLOD() const { return false; }

};

