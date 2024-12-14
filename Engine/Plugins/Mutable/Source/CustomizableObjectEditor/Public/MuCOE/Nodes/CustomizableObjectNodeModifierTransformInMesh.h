// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCOE/Nodes/CustomizableObjectNodeModifierBase.h"
#include "MuT/NodeModifier.h"

#include "CustomizableObjectNodeModifierTransformInMesh.generated.h"

namespace ENodeTitleType { enum Type : int; }

class UCustomizableObject;
class UCustomizableObjectNodeRemapPins;
class UEdGraphPin;

UCLASS()
class CUSTOMIZABLEOBJECTEDITOR_API UCustomizableObjectNodeModifierTransformInMesh : public UCustomizableObjectNodeModifierBase
{
	GENERATED_BODY()

public:
	/** Transform to apply to the bounding mesh before selecting for vertices to transform. */
	UPROPERTY(EditAnywhere, Category = BoundingMesh)
	FTransform BoundingMeshTransform = FTransform::Identity;

public:
	// EdGraphNode interface
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;

	// UCustomizableObjectNode interface
	virtual void AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins) override;
	virtual bool IsExperimental() const override { return true; }

	// UCustomizableObjectNodeModifierBase interface
	UEdGraphPin* OutputPin() const override;

	// Own interface
	UEdGraphPin* BoundingMeshPin() const;
	UEdGraphPin* TransformPin() const;
	
private:
	static const TCHAR* OutputPinName;
	static const TCHAR* BoundingMeshPinName;
	static const TCHAR* TransformPinName;
	
};

