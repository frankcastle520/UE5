// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCOE/Nodes/CustomizableObjectNodeModifierBase.h"
#include "MuT/NodeModifier.h"

#include "CustomizableObjectNodeModifierClipWithUVMask.generated.h"

namespace ENodeTitleType { enum Type : int; }

class UCustomizableObject;
class UCustomizableObjectNodeRemapPins;
class UEdGraphPin;

UCLASS()
class CUSTOMIZABLEOBJECTEDITOR_API UCustomizableObjectNodeModifierClipWithUVMask : public UCustomizableObjectNodeModifierBase
{
	GENERATED_BODY()

public:

	/** Materials in all other objects that activate this tags will be clipped with this UV mask. */
	UPROPERTY()
	TArray<FString> Tags_DEPRECATED;

	/** UV channel index that will be used to get the UVs to apply the clipping mask to. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = ClipOptions)
	int32 UVChannelForMask = 0;

	UPROPERTY(EditAnywhere, Category = ClipOptions)
	EFaceCullStrategy FaceCullStrategy = EFaceCullStrategy::AllVerticesCulled;

public:

	// EdGraphNode interface
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;

	// UCustomizableObjectNode interface
	virtual void AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins) override;
	virtual void PinConnectionListChanged(UEdGraphPin* Pin) override;
	virtual void BackwardsCompatibleFixup(int32 CustomizableObjectCustomVersion) override;


	// Own interface

	/** Access to input pins. */
	UEdGraphPin* ClipMaskPin() const;
};

