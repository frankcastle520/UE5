// Copyright Epic Games, Inc. All Rights Reserved.

#include "SCustomizableObjectNode.h"

#include "SCustomizableObjectNodePin.h"
#include "MuCOE/Nodes/CustomizableObjectNode.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "CustomizableObjectEditor"


void SCustomizableObjectNode::Construct(const FArguments& InArgs, UEdGraphNode* InGraphNode)
{
    GraphNode = InGraphNode;
	
    UpdateGraphNode();
}


TSharedPtr<SGraphPin> SCustomizableObjectNode::CreatePinWidget(UEdGraphPin* Pin) const
{
	return SNew(SCustomizableObjectNodePin, Pin);
}


void SCustomizableObjectNode::CreateBelowPinControls(TSharedPtr<SVerticalBox> MainBox)
{
	SGraphNode::CreateBelowPinControls(MainBox);

	if (UCustomizableObjectNode* Node = Cast<UCustomizableObjectNode>(GraphNode))
	{
		if (Node->IsExperimental())
		{
			MainBox->AddSlot()
			.Padding(FMargin(0.0, 2.0, 0.0, 0.0))
			.AutoHeight()
			[
				SNew(SErrorText)
				.BackgroundColor(FAppStyle::GetColor("ErrorReporting.WarningBackgroundColor"))
				.ErrorText(LOCTEXT("Experimental", "EXPERIMENTAL"))
			];
		}
	}
}


#undef LOCTEXT_NAMESPACE

