// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCOE/Nodes/CustomizableObjectNodeModifierBase.h"
#include "MuCOE/Nodes/CustomizableObjectNodeMaterialBase.h"

#include "CustomizableObjectNodeModifierWithMaterial.generated.h"

class UObject;


UCLASS(abstract)
class CUSTOMIZABLEOBJECTEDITOR_API UCustomizableObjectNodeModifierWithMaterial : public UCustomizableObjectNodeModifierBase
{

	GENERATED_BODY()

public:

	/** Reference material that defines the structure of the data to be extended.
	* The Sections modified with this modifier are supposed to have the same texture parameters, but don't need to have exactly
	* the ReferenceMaterial set.
	*/
	UPROPERTY(EditAnywhere, Category = Parameters)
	TObjectPtr<UMaterialInterface> ReferenceMaterial = nullptr;

	/** Relates a Parameter id (and layer if is a layered material) to a Pin. Only used to improve performance. */
	UPROPERTY()
	TMap<FNodeMaterialParameterId, FEdGraphPinReference> PinsParameterMap;

public:

	//
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	// UCustomizableObjectNode interface
	virtual bool IsNodeOutDatedAndNeedsRefresh() override;

	// Own interface

	/** */
	bool UsesImage(const FNodeMaterialParameterId& ImageId) const;

	/** */
	const UEdGraphPin* GetUsedImagePin(const FNodeMaterialParameterId& ImageId) const;

	/** */
	int32 GetNumParameters(const EMaterialParameterType Type) const;
	FNodeMaterialParameterId GetParameterId(const EMaterialParameterType Type, const int32 ParameterIndex) const;
	FName GetParameterName(const EMaterialParameterType Type, const int32 ParameterIndex) const;
	static int32 GetParameterLayerIndex(const UMaterialInterface* InMaterial, const EMaterialParameterType Type, const int32 ParameterIndex);
	int32 GetParameterLayerIndex(const EMaterialParameterType Type, const int32 ParameterIndex) const;
	FText GetParameterLayerName(const EMaterialParameterType Type, const int32 ParameterIndex) const;
};

