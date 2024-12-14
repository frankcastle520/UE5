// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCOE/Nodes/CustomizableObjectNodeComponentMeshBase.h"

#include "CustomizableObjectNodeComponentMeshAddTo.generated.h"

UCLASS()
class CUSTOMIZABLEOBJECTEDITOR_API UCustomizableObjectNodeComponentMeshAddTo : public UCustomizableObjectNodeComponentMeshBase
{
	GENERATED_BODY()

public:
	// UObject interface
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	// UEdGraphNode interface
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	
	UPROPERTY(EditAnywhere, Category = ComponentMesh)
	FName ParentComponentName;
};