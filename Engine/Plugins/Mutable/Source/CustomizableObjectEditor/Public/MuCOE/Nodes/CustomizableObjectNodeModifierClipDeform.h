// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCOE/Nodes/CustomizableObjectNodeModifierBase.h"
#include "MuT/NodeModifier.h"

#include "CustomizableObjectNodeModifierClipDeform.generated.h"

class UCustomizableObjectNodeRemapPins;
class UEdGraphPin;
class UObject;

UENUM()
enum class EShapeBindingMethod : uint32
{
	ClosestProject = 0,
	ClosestToSurface = 1,
	NormalProject = 2
};

UCLASS()
class CUSTOMIZABLEOBJECTEDITOR_API UCustomizableObjectNodeModifierClipDeform : public UCustomizableObjectNodeModifierBase
{

	GENERATED_BODY()

public:

	UPROPERTY()
	TArray<FString> Tags_DEPRECATED;

	UPROPERTY(EditAnywhere, Category = MeshClipDeform)
	EShapeBindingMethod BindingMethod;

	UPROPERTY(EditAnywhere, Category = RemoveOptions)
	EFaceCullStrategy FaceCullStrategy = EFaceCullStrategy::AllVerticesCulled;

public:

	// EdGraphNode interface
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;

	// UCustomizableObjectNode interface
	virtual void AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins) override;
	virtual void BackwardsCompatibleFixup(int32 CustomizableObjectCustomVersion) override;
	virtual bool IsExperimental() const override;

	// Own interface

	/** Input pins. */
	UEdGraphPin* ClipShapePin() const;
};

