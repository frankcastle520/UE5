// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HairStrandsMutableExtension.h"
#include "MuCOE/ICustomizableObjectExtensionNode.h"
#include "MuCOE/Nodes/CustomizableObjectNodeExtensionDataConstant.h"

#include "CustomizableObjectNodeGroomConstant.generated.h"

/** Imports a Groom into the Customizable Object graph */
UCLASS()
class UCustomizableObjectNodeGroomConstant : public UCustomizableObjectNodeExtensionDataConstant, public ICustomizableObjectExtensionNode
{
	GENERATED_BODY()

public:
	/** EdGraphNode interface */
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual FText GetTooltipText() const override;

	/** UCustomizableObjectNode interface */
	virtual void AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins) override;
	virtual bool ShouldAddToContextMenu(FText& OutCategory) const override;
	virtual bool IsExperimental() const override;
	
	/** ICustomizableObjectExtensionNode interface */
	virtual mu::Ptr<mu::NodeExtensionData> GenerateMutableNode(FExtensionDataCompilerInterface& CompilerInterface) const override;

	UPROPERTY(EditAnywhere, Category = CustomizableObject)
	FGroomPinData GroomData;
};

