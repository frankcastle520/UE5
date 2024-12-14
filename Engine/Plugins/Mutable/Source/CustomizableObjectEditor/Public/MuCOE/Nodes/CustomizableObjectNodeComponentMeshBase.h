// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCOE/Nodes/CustomizableObjectNodeComponent.h"

#include "CustomizableObjectNodeComponentMeshBase.generated.h"


UENUM()
enum class ECustomizableObjectAutomaticLODStrategy : uint8
{
	// Use the same strategy than the parent component. If root, then use "Manual".
	Inherited = 0 UMETA(DisplayName = "Inherit from parent component"),
	// Don't try to generate LODs automatically for the child nodes. Only the ones tha explicitely define them will be used.
	Manual = 1 UMETA(DisplayName = "Only manually created LODs"),
	// Try to generate the same material structure than LOd 0 if the source meshes have LODs.
	AutomaticFromMesh = 2 UMETA(DisplayName = "Automatic from mesh")
};


UCLASS()
class CUSTOMIZABLEOBJECTEDITOR_API UCustomizableObjectNodeComponentMeshBase : public UCustomizableObjectNodeComponent
{
	GENERATED_BODY()
	
public:
	// UEdGraphNode interface
	virtual void AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins) override;
	virtual FLinearColor GetNodeTitleColor() const override;

	// UCustomizableObjectNode interface
	virtual bool IsSingleOutputNode() const override;

	UPROPERTY(EditAnywhere, Category = ComponentMesh)
	int32 NumLODs = 1;
	
	UPROPERTY(EditAnywhere, Category = ComponentMesh, DisplayName = "Auto LOD Strategy")
	ECustomizableObjectAutomaticLODStrategy AutoLODStrategy = ECustomizableObjectAutomaticLODStrategy::AutomaticFromMesh;
	
	UPROPERTY()
	TArray<FEdGraphPinReference> LODPins;
	
	UPROPERTY()
	FEdGraphPinReference OutputPin;
};