// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/Nodes/CustomizableObjectNodeModifierTransformInMesh.h"

#include "MuCOE/EdGraphSchema_CustomizableObject.h"

#define LOCTEXT_NAMESPACE "CustomizableObjectEditor"


const TCHAR* UCustomizableObjectNodeModifierTransformInMesh::OutputPinName = TEXT("Modifier");
const TCHAR* UCustomizableObjectNodeModifierTransformInMesh::BoundingMeshPinName = TEXT("Bounding Mesh");
const TCHAR* UCustomizableObjectNodeModifierTransformInMesh::TransformPinName = TEXT("Transform");


FText UCustomizableObjectNodeModifierTransformInMesh::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("Transform_Mesh_In_Mesh", "Transform Mesh In Mesh");
}

FText UCustomizableObjectNodeModifierTransformInMesh::GetTooltipText() const
{
	return LOCTEXT("Transform_Mesh_In_Mesh_Tooltip", "Applies a transform to the vertices of a mesh that is contained within the given bounding mesh");
}

void UCustomizableObjectNodeModifierTransformInMesh::AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins)
{
	const UEdGraphSchema_CustomizableObject* Schema = GetDefault<UEdGraphSchema_CustomizableObject>();

	UEdGraphPin* ClipMeshPin = CustomCreatePin(EGPD_Input, Schema->PC_Mesh, FName(BoundingMeshPinName));
	ClipMeshPin->bDefaultValueIsIgnored = true;

	UEdGraphPin* TransformPin = CustomCreatePin(EGPD_Input, Schema->PC_Transform, FName(TransformPinName));
	TransformPin->bDefaultValueIsIgnored = true;
	
	(void)CustomCreatePin(EGPD_Output, Schema->PC_Modifier, FName(OutputPinName));
}

UEdGraphPin* UCustomizableObjectNodeModifierTransformInMesh::OutputPin() const
{
	return FindPin(OutputPinName);
}

UEdGraphPin* UCustomizableObjectNodeModifierTransformInMesh::BoundingMeshPin() const
{
	return FindPin(BoundingMeshPinName);
}

UEdGraphPin* UCustomizableObjectNodeModifierTransformInMesh::TransformPin() const
{
	return FindPin(TransformPinName);
}

#undef LOCTEXT_NAMESPACE
