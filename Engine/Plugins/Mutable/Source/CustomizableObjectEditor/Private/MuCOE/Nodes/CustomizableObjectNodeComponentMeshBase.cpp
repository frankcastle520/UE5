// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/Nodes/CustomizableObjectNodeComponentMeshBase.h"

#include "MuCOE/EdGraphSchema_CustomizableObject.h"


void UCustomizableObjectNodeComponentMeshBase::AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins)
{
	Super::AllocateDefaultPins(RemapPins);

	const UEdGraphSchema_CustomizableObject* Schema = GetDefault<UEdGraphSchema_CustomizableObject>();

	LODPins.Empty(NumLODs);
	for (int32 LODIndex = 0; LODIndex < NumLODs; ++LODIndex)
	{
		FString LODName = FString::Printf(TEXT("LOD %d"), LODIndex);

		UEdGraphPin* Pin = CustomCreatePin(EGPD_Input, Schema->PC_Material, FName(*LODName), true);
		LODPins.Add(Pin);
	}
	
	OutputPin = CustomCreatePin(EGPD_Output, Schema->PC_Component, TEXT("Component"));
}


FLinearColor UCustomizableObjectNodeComponentMeshBase::GetNodeTitleColor() const
{
	const UEdGraphSchema_CustomizableObject* Schema = GetDefault<UEdGraphSchema_CustomizableObject>();
	return Schema->GetPinTypeColor(Schema->PC_Component);
}

bool UCustomizableObjectNodeComponentMeshBase::IsSingleOutputNode() const
{
	// todo UE-225446 : By limiting the number of connections this node can have we avoid a check failure. However, this method should be
	// removed in the future and the inherent issue with 1:n output connections should be fixed in its place
	return true;
}

