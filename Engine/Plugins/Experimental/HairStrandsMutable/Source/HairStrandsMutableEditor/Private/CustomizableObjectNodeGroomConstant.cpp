// Copyright Epic Games, Inc. All Rights Reserved.

#include "CustomizableObjectNodeGroomConstant.h"

#include "MuCOE/EdGraphSchema_CustomizableObject.h"
#include "MuCOE/ExtensionDataCompilerInterface.h"
#include "MuCOE/RemapPins/CustomizableObjectNodeRemapPins.h"
#include "MuR/ExtensionData.h"
#include "MuT/NodeExtensionDataConstant.h"
#include "HairStrandsMutableExtension.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(CustomizableObjectNodeGroomConstant)

#define LOCTEXT_NAMESPACE "HairStrandsMutableEditor"

FText UCustomizableObjectNodeGroomConstant::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("Groom_Constant", "Groom Constant");
}

FLinearColor UCustomizableObjectNodeGroomConstant::GetNodeTitleColor() const
{
	const UEdGraphSchema_CustomizableObject* Schema = GetDefault<UEdGraphSchema_CustomizableObject>();
	return Schema->GetPinTypeColor(UHairStrandsMutableExtension::GroomPinType);
}

FText UCustomizableObjectNodeGroomConstant::GetTooltipText() const
{
	return LOCTEXT("Groom_Constant_Tooltip", "Imports a Groom");
}

void UCustomizableObjectNodeGroomConstant::AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins)
{
	UEdGraphPin* OutputPin = CustomCreatePin(EGPD_Output, UHairStrandsMutableExtension::GroomPinType, UHairStrandsMutableExtension::GroomsBaseNodePinName);
	OutputPin->bDefaultValueIsIgnored = true;
}

bool UCustomizableObjectNodeGroomConstant::ShouldAddToContextMenu(FText& OutCategory) const
{
	OutCategory = UEdGraphSchema_CustomizableObject::NC_Experimental;
	return true;
}


bool UCustomizableObjectNodeGroomConstant::IsExperimental() const
{
	return true;
}


mu::Ptr<mu::NodeExtensionData> UCustomizableObjectNodeGroomConstant::GenerateMutableNode(FExtensionDataCompilerInterface& CompilerInterface) const
{
	check(IsInGameThread());

	// Create node and extension data container
	mu::Ptr<mu::NodeExtensionDataConstant> Result = new mu::NodeExtensionDataConstant();
	
	// Grooms are usually quite large, so set it up as a streaming constant to allow it to be
	// loaded on demand.
	//
	// If needed we could expose an editable UPROPERTY to give the user the option of making this
	// an always-loaded constant.
	UCustomizableObjectResourceDataContainer* Container = nullptr;
	Result->SetValue(CompilerInterface.MakeStreamedExtensionData(Container));

	if (Container)
	{
		// Populate instanced struct
		Container->Data.Data.InitializeAs<FGroomPinData>(GroomData);
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE
