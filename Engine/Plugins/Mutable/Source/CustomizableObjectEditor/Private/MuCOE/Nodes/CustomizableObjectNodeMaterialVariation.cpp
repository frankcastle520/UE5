// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/Nodes/CustomizableObjectNodeMaterialVariation.h"

#include "MuCO/CustomizableObjectCustomVersion.h"
#include "MuCOE/CustomizableObjectEditor_Deprecated.h"
#include "MuCOE/EdGraphSchema_CustomizableObject.h"


void UCustomizableObjectNodeMaterialVariation::BackwardsCompatibleFixup(int32 CustomizableObjectCustomVersion)
{
	Super::BackwardsCompatibleFixup(CustomizableObjectCustomVersion);

	if (CustomizableObjectCustomVersion == FCustomizableObjectCustomVersion::NodeVariationSerializationIssue)
	{
		for (const FCustomizableObjectMaterialVariation& OldVariation : Variations_DEPRECATED)
		{
			FCustomizableObjectVariation Variation;
			Variation.Tag = OldVariation.Tag;
			
			VariationsData.Add(Variation);
		}
	}
}


FName UCustomizableObjectNodeMaterialVariation::GetCategory() const
{
	return UEdGraphSchema_CustomizableObject::PC_Material;
}


bool UCustomizableObjectNodeMaterialVariation::IsInputPinArray() const
{
	return true;
}


bool UCustomizableObjectNodeMaterialVariation::IsSingleOutputNode() const
{
	return true;
}
