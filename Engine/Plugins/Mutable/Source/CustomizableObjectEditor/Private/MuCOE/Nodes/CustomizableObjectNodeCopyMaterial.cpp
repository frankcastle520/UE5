// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/Nodes/CustomizableObjectNodeCopyMaterial.h"

#include "MuCOE/EdGraphSchema_CustomizableObject.h"
#include "MuCOE/GraphTraversal.h"
#include "MuCOE/Nodes/CustomizableObjectNodeExposePin.h"
#include "MuCOE/Nodes/CustomizableObjectNodeExternalPin.h"
#include "MuCOE/Nodes/CustomizableObjectNodeObject.h"
#include "MuCOE/Nodes/CustomizableObjectNodeSkeletalMesh.h"
#include "MuCOE/Nodes/CustomizableObjectNodeMaterial.h"
#include "MuCO/CustomizableObjectCustomVersion.h"

#define LOCTEXT_NAMESPACE "CustomizableObjectEditor"


/** Mesh input pin key */
const static FString MeshPinName = TEXT("Mesh_Input_Pin");

/** Material input pin key */
const static FString MeshSectionPinName = TEXT("MeshSection_Input_Pin");

/** Material output pin key*/
const static FString OutputPinName = TEXT("MeshSection_Output_Pin");


FText UCustomizableObjectNodeCopyMaterial::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("Copy_MeshSection", "Copy Mesh Section");
}


void UCustomizableObjectNodeCopyMaterial::AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins)
{
	const UEdGraphSchema_CustomizableObject* Schema = GetDefault<UEdGraphSchema_CustomizableObject>();

	// Input pins
	UEdGraphPin* Pin = CustomCreatePin(EGPD_Input, Schema->PC_Mesh, FName(MeshPinName));
	Pin->PinFriendlyName = LOCTEXT("MeshPin", "Mesh");
	Pin->bDefaultValueIsIgnored = true;

	Pin = CustomCreatePin(EGPD_Input, Schema->PC_Material, FName(MeshSectionPinName));
	Pin->PinFriendlyName = LOCTEXT("BaseMeshSectionPin", "Base Mesh Section");;
	Pin->bDefaultValueIsIgnored = true;

	// Output pins
	Pin = CustomCreatePin(EGPD_Output, Schema->PC_Material, FName(OutputPinName));
	Pin->PinFriendlyName = LOCTEXT("MeshSectionPin", "Mesh Section");
}


void UCustomizableObjectNodeCopyMaterial::BackwardsCompatibleFixup(int32 CustomizableObjectCustomVersion)
{
	Super::BackwardsCompatibleFixup(CustomizableObjectCustomVersion);

	if (CustomizableObjectCustomVersion == FCustomizableObjectCustomVersion::MaterialPinsRename)
	{
		UEdGraphPin* BaseMaterialPin = FindPin(TEXT("Material_Input_Pin"), EEdGraphPinDirection::EGPD_Input);
		if (BaseMaterialPin)
		{
			const FString PinFriendlyName = TEXT("Base Mesh Section");
			BaseMaterialPin->PinName = FName(MeshSectionPinName);
			BaseMaterialPin->PinFriendlyName = FText::FromString(PinFriendlyName);
		}

		UEdGraphPin* MaterialPin = FindPin(TEXT("Material_Output_Pin"), EEdGraphPinDirection::EGPD_Output);
		if (MaterialPin)
		{
			const FString PinFriendlyName = TEXT("Mesh Section");
			MaterialPin->PinName = FName(OutputPinName);
			MaterialPin->PinFriendlyName = FText::FromString(PinFriendlyName);
		}
	}
}


UEdGraphPin* UCustomizableObjectNodeCopyMaterial::GetMeshPin() const
{
	return FindPin(MeshPinName);
}


FPostImagePinModeChangedDelegate* UCustomizableObjectNodeCopyMaterial::GetPostImagePinModeChangedDelegate()
{
	if (UCustomizableObjectNodeMaterial* NodeMaterial = GetMaterialNode())
	{
		return NodeMaterial->GetPostImagePinModeChangedDelegate();
	}
	else
	{
		return nullptr;
	}
}


TArray<UCustomizableObjectLayout*> UCustomizableObjectNodeCopyMaterial::GetLayouts() const
{
	if (UCustomizableObjectNodeMaterial* NodeMaterial = GetMaterialNode())
	{
		return NodeMaterial->GetLayouts();
	}
	else
	{
		return {};
	}
}


UEdGraphPin* UCustomizableObjectNodeCopyMaterial::OutputPin() const
{
	UEdGraphPin* Pin = FindPin(OutputPinName, EEdGraphPinDirection::EGPD_Output);

	// Legacy name
	if (!Pin)
	{
		Pin = FindPin(TEXT("Material_Output_Pin"), EEdGraphPinDirection::EGPD_Output);
	}

	return Pin;
}


bool UCustomizableObjectNodeCopyMaterial::RealMaterialDataHasChanged() const
{
	if (UCustomizableObjectNodeMaterial* NodeMaterial = GetMaterialNode())
	{
		return NodeMaterial->RealMaterialDataHasChanged();
	}
	else
	{
		return false;
	}
}


UEdGraphPin* UCustomizableObjectNodeCopyMaterial::GetMeshSectionPin() const
{
	UEdGraphPin* Pin = FindPin(MeshSectionPinName, EEdGraphPinDirection::EGPD_Input);

	// Legacy name
	if (!Pin)
	{
		Pin = FindPin(TEXT("Material_Input_Pin"), EEdGraphPinDirection::EGPD_Input);
	}

	return Pin;
}


UCustomizableObjectNodeSkeletalMesh* UCustomizableObjectNodeCopyMaterial::GetMeshNode() const
{
	UCustomizableObjectNodeSkeletalMesh* Result = nullptr;

	UEdGraphPin* MaterialPin = GetMeshPin();
	if (const UEdGraphPin* ConnectedPin = FollowInputPin(*MaterialPin))
	{
		const UEdGraphPin* SourceMeshPin = FindMeshBaseSource(*ConnectedPin, false);
		if (SourceMeshPin)
		{
			Result = Cast<UCustomizableObjectNodeSkeletalMesh>(SourceMeshPin->GetOwningNode());
		}
	}

	return Result;
}


UCustomizableObjectNodeMaterial* UCustomizableObjectNodeCopyMaterial::GetMaterialNode() const
{
	UCustomizableObjectNodeMaterial* Result = nullptr;

	UEdGraphPin* MaterialPin = GetMeshSectionPin();
	if (const UEdGraphPin* ConnectedPin = FollowInputPin(*MaterialPin))
	{
		return Cast<UCustomizableObjectNodeMaterial>(ConnectedPin->GetOwningNode());
	}

	return Result;
}


bool UCustomizableObjectNodeCopyMaterial::CanConnect(const UEdGraphPin* InOwnedInputPin, const UEdGraphPin* InOutputPin, bool& bOutIsOtherNodeBlocklisted, bool& bOutArePinsCompatible) const
{
	if (!Super::CanConnect(InOwnedInputPin, InOutputPin, bOutIsOtherNodeBlocklisted, bOutIsOtherNodeBlocklisted))
	{
		return false;
	}

	if (InOwnedInputPin == GetMeshSectionPin())
	{
		const UEdGraphNode* OuputPinOwningNode = InOutputPin->GetOwningNode();
		return (OuputPinOwningNode->IsA(UCustomizableObjectNodeMaterial::StaticClass()) && !OuputPinOwningNode->IsA(UCustomizableObjectNodeCopyMaterial::StaticClass()))
			|| OuputPinOwningNode->IsA(UCustomizableObjectNodeExternalPin::StaticClass());
	}

	return true;
}


bool UCustomizableObjectNodeCopyMaterial::ShouldBreakExistingConnections(const UEdGraphPin* InputPin, const UEdGraphPin* OutputPin) const
{
	return true;
}


bool UCustomizableObjectNodeCopyMaterial::IsNodeOutDatedAndNeedsRefresh()
{
	return false;
}


bool UCustomizableObjectNodeCopyMaterial::ProvidesCustomPinRelevancyTest() const
{
	return true;
}


bool UCustomizableObjectNodeCopyMaterial::IsPinRelevant(const UEdGraphPin* Pin) const
{
	const UEdGraphNode* Node = Pin->GetOwningNode();

	if (Pin->Direction == EGPD_Output)
	{
		return (Node->IsA(UCustomizableObjectNodeMaterial::StaticClass()) && !Node->IsA(UCustomizableObjectNodeCopyMaterial::StaticClass())) ||
			(Node->IsA(UCustomizableObjectNodeExternalPin::StaticClass()) && Pin->PinType.PinCategory == UEdGraphSchema_CustomizableObject::PC_Material) ||
			Pin->PinType.PinCategory == UEdGraphSchema_CustomizableObject::PC_Mesh;
	}
	else
	{
		return Node->IsA(UCustomizableObjectNodeObject::StaticClass()) ||
			(Node->IsA(UCustomizableObjectNodeExposePin::StaticClass()) && Pin->PinType.PinCategory == UEdGraphSchema_CustomizableObject::PC_Material);
			
	}
}


UMaterialInterface* UCustomizableObjectNodeCopyMaterial::GetMaterial() const
{
	if (UCustomizableObjectNodeMaterial* NodeMaterial = GetMaterialNode())
	{
		return NodeMaterial->GetMaterial();
	}
	else
	{
		return nullptr;
	}
}


bool UCustomizableObjectNodeCopyMaterial::IsReuseMaterialBetweenLODs() const
{
	if (UCustomizableObjectNodeMaterial* NodeMaterial = GetMaterialNode())
	{
		return NodeMaterial->IsReuseMaterialBetweenLODs();
	}
	else
	{
		return false;
	}
}


TArray<FString>* UCustomizableObjectNodeCopyMaterial::GetEnableTags()
{
	if (UCustomizableObjectNodeMaterial* NodeMaterial = GetMaterialNode())
	{
		return NodeMaterial->GetEnableTags();
	}
	else
	{
		return nullptr;
	}
}


UEdGraphPin* UCustomizableObjectNodeCopyMaterial::GetMaterialAssetPin() const
{
	if (UCustomizableObjectNodeMaterial* NodeMaterial = GetMaterialNode())
	{
		return NodeMaterial->GetMaterialAssetPin();
	}
	else
	{
		return nullptr;
	}
}


int32 UCustomizableObjectNodeCopyMaterial::GetNumParameters(EMaterialParameterType Type) const
{
	if (UCustomizableObjectNodeMaterial* NodeMaterial = GetMaterialNode())
	{
		return NodeMaterial->GetNumParameters(Type);
	}
	else
	{
		return 0;
	}
}


FNodeMaterialParameterId UCustomizableObjectNodeCopyMaterial::GetParameterId(EMaterialParameterType Type, int32 ParameterIndex) const
{
	UCustomizableObjectNodeMaterial* NodeMaterial = GetMaterialNode();
	check(NodeMaterial);

	return NodeMaterial->GetParameterId(Type, ParameterIndex);
}


FName UCustomizableObjectNodeCopyMaterial::GetParameterName(EMaterialParameterType Type, int32 ParameterIndex) const
{
	UCustomizableObjectNodeMaterial* NodeMaterial = GetMaterialNode();
	check(NodeMaterial);
	
	return NodeMaterial->GetParameterName(Type, ParameterIndex);
}


int32 UCustomizableObjectNodeCopyMaterial::GetParameterLayerIndex(EMaterialParameterType Type, int32 ParameterIndex) const
{
	UCustomizableObjectNodeMaterial* NodeMaterial = GetMaterialNode();
	check(NodeMaterial);
	
	return NodeMaterial->GetParameterLayerIndex(Type, ParameterIndex);
}


FText UCustomizableObjectNodeCopyMaterial::GetParameterLayerName(EMaterialParameterType Type, int32 ParameterIndex) const
{
	UCustomizableObjectNodeMaterial* NodeMaterial = GetMaterialNode();
	check(NodeMaterial);
	
	return NodeMaterial->GetParameterLayerName(Type, ParameterIndex);
}


bool UCustomizableObjectNodeCopyMaterial::HasParameter(const FNodeMaterialParameterId& ParameterId) const
{
	if (UCustomizableObjectNodeMaterial* NodeMaterial = GetMaterialNode())
	{
		return NodeMaterial->HasParameter(ParameterId);
	}
	else
	{
		return false;	
	}
}


const UEdGraphPin* UCustomizableObjectNodeCopyMaterial::GetParameterPin(EMaterialParameterType Type, int32 ParameterIndex) const
{
	if (UCustomizableObjectNodeMaterial* NodeMaterial = GetMaterialNode())
	{
		return NodeMaterial->GetParameterPin(Type, ParameterIndex);
	}
	else
	{
		return nullptr;	
	}
}


UEdGraphPin* UCustomizableObjectNodeCopyMaterial::GetParameterPin(const FNodeMaterialParameterId& ParameterId) const
{
	if (UCustomizableObjectNodeMaterial* NodeMaterial = GetMaterialNode())
	{
		return NodeMaterial->GetParameterPin(ParameterId);
	}
	else
	{
		return nullptr;	
	}
}


bool UCustomizableObjectNodeCopyMaterial::IsImageMutableMode(int32 ImageIndex) const
{
	UCustomizableObjectNodeMaterial* NodeMaterial = GetMaterialNode();
	check(NodeMaterial);
	
	return NodeMaterial->IsImageMutableMode(ImageIndex);
}


bool UCustomizableObjectNodeCopyMaterial::IsImageMutableMode(const UEdGraphPin& Pin) const
{
	UCustomizableObjectNodeMaterial* NodeMaterial = GetMaterialNode();
	check(NodeMaterial);

	return NodeMaterial->IsImageMutableMode(Pin);
}


UTexture2D* UCustomizableObjectNodeCopyMaterial::GetImageReferenceTexture(int32 ImageIndex) const
{
	if (UCustomizableObjectNodeMaterial* NodeMaterial = GetMaterialNode())
	{
		return NodeMaterial->GetImageReferenceTexture(ImageIndex);
	}
	else
	{
		return nullptr;
	}
}


UTexture2D* UCustomizableObjectNodeCopyMaterial::GetImageValue(int32 ImageIndex) const
{
	if (UCustomizableObjectNodeMaterial* NodeMaterial = GetMaterialNode())
	{
		return NodeMaterial->GetImageValue(ImageIndex);
	}
	else
	{
		return nullptr;
	}
}


int32 UCustomizableObjectNodeCopyMaterial::GetImageUVLayout(int32 ImageIndex) const
{
	if (UCustomizableObjectNodeMaterial* NodeMaterial = GetMaterialNode())
	{
		return NodeMaterial->GetImageUVLayout(ImageIndex);
	}
	else
	{
		return UCustomizableObjectNodeMaterialPinDataImage::UV_LAYOUT_IGNORE;
	}
}


FText UCustomizableObjectNodeCopyMaterial::GetTooltipText() const
{
	return LOCTEXT("CopyMaterial_Tooltip", "Copies a Customizable Object material.\nDuplicates all Material node input pins and properties except for the Mesh input pin.");
}


#undef LOCTEXT_NAMESPACE
