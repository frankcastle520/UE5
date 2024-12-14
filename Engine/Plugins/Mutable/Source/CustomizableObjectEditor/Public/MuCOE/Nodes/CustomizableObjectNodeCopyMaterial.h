// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCOE/Nodes/CustomizableObjectNodeMaterial.h"

#include "CustomizableObjectNodeCopyMaterial.generated.h"

class UCustomizableObjectNodeRemapPins;
class UCustomizableObjectNodeSkeletalMesh;
class UEdGraphPin;
class UObject;


/** Generates a new Surface with the same connections as the Parent Material Surface, but with a different Mesh. */
UCLASS()
class CUSTOMIZABLEOBJECTEDITOR_API UCustomizableObjectNodeCopyMaterial : public UCustomizableObjectNodeMaterialBase
{
public:
	GENERATED_BODY()

	// UEdGraphNode interface
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;

	// UCustomizableObjectNode interface
	virtual void AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins) override;
	virtual bool CanConnect(const UEdGraphPin* InOwnedInputPin, const UEdGraphPin* InOutputPin, bool& bOutIsOtherNodeBlocklisted, bool& bOutArePinsCompatible) const override;
	virtual bool ShouldBreakExistingConnections(const UEdGraphPin* InputPin, const UEdGraphPin* OutputPin) const override;
	virtual bool IsNodeOutDatedAndNeedsRefresh() override;
	virtual bool ProvidesCustomPinRelevancyTest() const override;
	virtual bool IsPinRelevant(const UEdGraphPin* Pin) const override;
	virtual void BackwardsCompatibleFixup(int32 CustomizableObjectCustomVersion) override;

	// UCustomizableObjectNodeMaterialBase interface
	virtual UMaterialInterface* GetMaterial() const override;
	virtual bool IsReuseMaterialBetweenLODs() const override;
	virtual TArray<FString>* GetEnableTags() override;
	virtual UEdGraphPin* GetMaterialAssetPin() const override;
	virtual int32 GetNumParameters(EMaterialParameterType Type) const override;
	virtual FNodeMaterialParameterId GetParameterId(EMaterialParameterType Type, int32 ParameterIndex) const override;
	virtual FName GetParameterName(EMaterialParameterType Type, int32 ParameterIndex) const override;
	virtual int32 GetParameterLayerIndex(EMaterialParameterType Type, int32 ParameterIndex) const override;
	virtual FText GetParameterLayerName(EMaterialParameterType Type, int32 ParameterIndex) const override;
	virtual bool HasParameter(const FNodeMaterialParameterId& ParameterId) const override;
	virtual const UEdGraphPin* GetParameterPin(EMaterialParameterType Type, int32 ParameterIndex) const override;
	virtual UEdGraphPin* GetParameterPin(const FNodeMaterialParameterId& ParameterId) const override;
	virtual bool IsImageMutableMode(int32 ImageIndex) const override;
	virtual bool IsImageMutableMode(const UEdGraphPin& Pin) const override;
	virtual UTexture2D* GetImageReferenceTexture(int32 ImageIndex) const override;
	virtual UTexture2D* GetImageValue(int32 ImageIndex) const override;
	virtual int32 GetImageUVLayout(int32 ImageIndex) const override;
	virtual UCustomizableObjectNodeMaterial* GetMaterialNode() const override;
	virtual UEdGraphPin* GetMeshPin() const override;
	virtual FPostImagePinModeChangedDelegate* GetPostImagePinModeChangedDelegate() override;
	virtual TArray<UCustomizableObjectLayout*> GetLayouts() const override;
	virtual UEdGraphPin* OutputPin() const override;
	virtual bool RealMaterialDataHasChanged() const override;
	
	// Own interface
	UCustomizableObjectNodeSkeletalMesh* GetMeshNode() const;
	
protected:

	UEdGraphPin* GetMeshSectionPin() const;

};
