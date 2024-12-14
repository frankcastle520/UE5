// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCOE/ICustomizableObjectExtensionNode.h"
#include "MuCOE/Nodes/CustomizableObjectNodeExtensionDataConstant.h"

#include "CustomizableObjectNodeDNAConstant.generated.h"

/** Imports DNA from a Skeletal Mesh into the Customizable Object graph */
UCLASS()
class UCustomizableObjectNodeDNAConstant : public UCustomizableObjectNodeExtensionDataConstant, public ICustomizableObjectExtensionNode
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

	/** The Skeletal Mesh to copy DNA from */
	UPROPERTY(EditAnywhere, Category = CustomizableObject)
	TObjectPtr<USkeletalMesh> SkeletalMesh;

	/** The index of the mesh component in the Customizable Object that this DNA will go to */
	UPROPERTY(EditAnywhere, Category = CustomizableObject)
	int32 ComponentIndex;
};

