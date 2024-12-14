// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/Nodes/CustomizableObjectNodeModifierMorphMeshSection.h"

#include "Engine/SkeletalMesh.h"
#include "MuCO/CustomizableObjectCustomVersion.h"
#include "MuCOE/EdGraphSchema_CustomizableObject.h"
#include "MuCOE/GraphTraversal.h"
#include "MuCOE/ICustomizableObjectEditor.h"
#include "MuCOE/Nodes/CustomizableObjectNodeMaterial.h"
#include "MuCOE/Nodes/CustomizableObjectNodeSkeletalMesh.h"

class UCustomizableObjectNodeRemapPins;
struct FPropertyChangedEvent;

#define LOCTEXT_NAMESPACE "CustomizableObjectEditor"


void UCustomizableObjectNodeModifierMorphMeshSection::AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins)
{
	const UEdGraphSchema_CustomizableObject* Schema = GetDefault<UEdGraphSchema_CustomizableObject>();

	FString PinName = TEXT("Modifier");
	UEdGraphPin* ModifierPin = CustomCreatePin(EGPD_Output, Schema->PC_Modifier, FName(PinName));
	ModifierPin->bDefaultValueIsIgnored = true;

	PinName = TEXT("Factor");
	UEdGraphPin* FactorPin = CustomCreatePin(EGPD_Input, Schema->PC_Float, FName(*PinName));
	FactorPin->bDefaultValueIsIgnored = true;
}


FText UCustomizableObjectNodeModifierMorphMeshSection::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("Morph_MeshSection", "Morph Mesh Section");
}


FString UCustomizableObjectNodeModifierMorphMeshSection::GetRefreshMessage() const
{
	return "Morph Target not found in the SkeletalMesh. Please Refresh Node and select a valid morph option.";
}


FText UCustomizableObjectNodeModifierMorphMeshSection::GetTooltipText() const
{
	return LOCTEXT("Morph_Material_Tooltip", "Fully activate one morph of a parent's material.");
}


bool UCustomizableObjectNodeModifierMorphMeshSection::IsSingleOutputNode() const
{
	return true;
}


#undef LOCTEXT_NAMESPACE
