// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/Nodes/CustomizableObjectNodeModifierRemoveMesh.h"

#include "MuCOE/EdGraphSchema_CustomizableObject.h"
#include "MuCOE/ICustomizableObjectEditor.h"
#include "MuCO/CustomizableObjectCustomVersion.h"

class UCustomizableObjectNodeRemapPins;

#define LOCTEXT_NAMESPACE "CustomizableObjectEditor"


void UCustomizableObjectNodeModifierRemoveMesh::AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins)
{
	const UEdGraphSchema_CustomizableObject* Schema = GetDefault<UEdGraphSchema_CustomizableObject>();

	UEdGraphPin* RemoveMeshPin = CustomCreatePin(EGPD_Input, Schema->PC_Mesh, FName("Remove Mesh") );
	RemoveMeshPin->bDefaultValueIsIgnored = true;

	UEdGraphPin* OutputPin = CustomCreatePin(EGPD_Output, Schema->PC_Modifier, FName("Modifier"));
}


FText UCustomizableObjectNodeModifierRemoveMesh::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("Remove_Mesh", "Remove Mesh");
}


void UCustomizableObjectNodeModifierRemoveMesh::PinConnectionListChanged(UEdGraphPin* Pin)
{
	Super::PinConnectionListChanged(Pin);

	if (Pin == OutputPin())
	{
		TSharedPtr<ICustomizableObjectEditor> Editor = GetGraphEditor();

		if (Editor.IsValid())
		{
			Editor->UpdateGraphNodeProperties();
		}
	}
}

FText UCustomizableObjectNodeModifierRemoveMesh::GetTooltipText() const
{
	return LOCTEXT("Remove_Mesh_Tooltip",
	"Removes the faces of a material that are defined only by vertexes shared by the material and the input mesh.It also removes any vertex\nand edge that only define deleted faces, they are not left dangling. If the mesh removed covers at least all the faces included in one or\nmore layout blocs, those blocs are removed, freeing final texture layout space.");
}

bool UCustomizableObjectNodeModifierRemoveMesh::IsSingleOutputNode() const
{
	return true;
}


#undef LOCTEXT_NAMESPACE
