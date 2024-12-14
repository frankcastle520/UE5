// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowSchema.h"

#include "Dataflow/DataflowCoreNodes.h"
#include "Dataflow/DataflowEdNode.h"
#include "Dataflow/DataflowEditorCommands.h"
#include "Dataflow/DataflowEdNode.h"
#include "Dataflow/DataflowSNode.h"
#include "Dataflow/DataflowNodeFactory.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraph.h"
#include "Framework/Commands/GenericCommands.h"
#include "Logging/LogMacros.h"
#include "ToolMenuSection.h"
#include "ToolMenu.h"
#include "GraphEditorActions.h"
#include "Dataflow/DataflowSettings.h"
#include "ScopedTransaction.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DataflowSchema)

#define LOCTEXT_NAMESPACE "DataflowNode"

namespace UE::Dataflow::Private
{
	static const FName ManagedArrayCollectionType = FName("FManagedArrayCollection");
	static const FName FloatType = FName("float");
	static const FName DoubleType = FName("double");
	static const FName Int32Type = FName("int32");
	static const FName BoolType = FName("bool");
	static const FName StringType = FName("FString");
	static const FName NameType = FName("FName");
	static const FName TextType = FName("FText");
	static const FName VectorType = FName("FVector");
	static const FName TransformType = FName("FTransform");
	static const FName RotatorType = FName("FRotator");
	static const FName ArrayType = FName("TArray");
	static const FName BoxType = FName("FBox");
	static const FName SphereType = FName("FSphere");
	static const FName DataflowAnyTypeType = FName("FDataflowAnyType");
} // namespace UE::Dataflow::Private

UDataflowSchema::UDataflowSchema()
{
}

void UDataflowSchema::GetContextMenuActions(class UToolMenu* Menu, class UGraphNodeContextMenuContext* Context) const
{
	if (Context->Node && !Context->Pin)
	{
		{
			FToolMenuSection& Section = Menu->AddSection("TestGraphSchemaNodeActions", LOCTEXT("GraphSchemaNodeActions_MenuHeader", "Node Actions"));
			{
				Section.AddMenuEntry(FGenericCommands::Get().Rename);
				Section.AddMenuEntry(FGenericCommands::Get().Delete);
				Section.AddMenuEntry(FGenericCommands::Get().Cut);
				Section.AddMenuEntry(FGenericCommands::Get().Copy);
				Section.AddMenuEntry(FGenericCommands::Get().Duplicate);
				Section.AddMenuEntry(FDataflowEditorCommands::Get().ToggleEnabledState, FText::FromString("Toggle Enabled State"));
				Section.AddMenuEntry(FGraphEditorCommands::Get().BreakNodeLinks);
				Section.AddMenuEntry(FDataflowEditorCommands::Get().AddOptionPin);
				Section.AddMenuEntry(FDataflowEditorCommands::Get().RemoveOptionPin);
				Section.AddMenuEntry(FDataflowEditorCommands::Get().EvaluateNode);
			}
		}
		{
			FToolMenuSection& Section = Menu->AddSection("TestGraphSchemaOrganization", LOCTEXT("GraphSchemaOrganization_MenuHeader", "Organization"));
			{
				Section.AddSubMenu("Alignment", LOCTEXT("AlignmentHeader", "Alignment"), FText(), FNewToolMenuDelegate::CreateLambda([](UToolMenu* AlignmentMenu)
				{
					{
						FToolMenuSection& InSection = AlignmentMenu->AddSection("TestGraphSchemaAlignment", LOCTEXT("GraphSchemaAlignment_MenuHeader", "Align"));

						InSection.AddMenuEntry(FGraphEditorCommands::Get().AlignNodesTop);
						InSection.AddMenuEntry(FGraphEditorCommands::Get().AlignNodesMiddle);
						InSection.AddMenuEntry(FGraphEditorCommands::Get().AlignNodesBottom);
						InSection.AddMenuEntry(FGraphEditorCommands::Get().AlignNodesLeft);
						InSection.AddMenuEntry(FGraphEditorCommands::Get().AlignNodesCenter);
						InSection.AddMenuEntry(FGraphEditorCommands::Get().AlignNodesRight);
						InSection.AddMenuEntry(FGraphEditorCommands::Get().StraightenConnections);
					}

					{
						FToolMenuSection& InSection = AlignmentMenu->AddSection("TestGraphSchemaDistribution", LOCTEXT("GraphSchemaDistribution_MenuHeader", "Distribution"));
						InSection.AddMenuEntry(FGraphEditorCommands::Get().DistributeNodesHorizontally);
						InSection.AddMenuEntry(FGraphEditorCommands::Get().DistributeNodesVertically);
					}
				}));
			}
		}
		{
			FToolMenuSection& Section = Menu->AddSection("TestGraphSchemaDisplay", LOCTEXT("GraphSchemaDisplay_MenuHeader", "Display"));
			{
				Section.AddSubMenu("PinVisibility", LOCTEXT("PinVisibilityHeader", "Pin Visibility"), FText(),
					FNewToolMenuDelegate::CreateLambda([](UToolMenu* PinVisibilityMenu)
				{
					FToolMenuSection& InSection = PinVisibilityMenu->AddSection("TestGraphSchemaPinVisibility");
					InSection.AddMenuEntry(FGraphEditorCommands::Get().ShowAllPins);
					InSection.AddMenuEntry(FGraphEditorCommands::Get().HideNoConnectionPins);
				}));
			}

		}
	}
	Super::GetContextMenuActions(Menu, Context);
}

void UDataflowSchema::GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const
{
	if (UE::Dataflow::FNodeFactory* Factory = UE::Dataflow::FNodeFactory::GetInstance())
	{
		for (UE::Dataflow::FFactoryParameters NodeParameters : Factory->RegisteredParameters())
		{
			if (TSharedPtr<FAssetSchemaAction_Dataflow_CreateNode_DataflowEdNode> Action =
				FAssetSchemaAction_Dataflow_CreateNode_DataflowEdNode::CreateAction(ContextMenuBuilder.CurrentGraph, NodeParameters.TypeName, NodeParameters.DisplayName))
			{
				ContextMenuBuilder.AddAction(Action);
			}
		}
	}
}

bool HasLoopIfConnected(const UEdGraphNode* FromNode, const UEdGraphNode* ToNode)
{
	if (ToNode == FromNode)
	{
		return true;
	}

	// We only need to process from the FromNode and test if anything in the feeding nodes contains ToNode
	TArray<const UEdGraphNode*> NodesToProcess;
	NodesToProcess.Push(FromNode);

	// to speed things up, we do not revisit branches we have already look at  
	TSet<const UEdGraphNode*> VisitedNodes;

	while (NodesToProcess.Num() > 0)
	{
		const UEdGraphNode* NodeToProcess = NodesToProcess.Pop();
		if (!VisitedNodes.Contains(NodeToProcess))
		{
			VisitedNodes.Add(NodeToProcess);

			int32 NumConnectedInputPins = 0;
			for (UEdGraphPin* Pin : NodeToProcess->GetAllPins())
			{
				if (Pin->Direction == EEdGraphPinDirection::EGPD_Input)
				{
					if (Pin->HasAnyConnections())
					{
						NumConnectedInputPins++;
						if (ensure(Pin->LinkedTo.Num() == 1))
						{
							if (const UEdGraphNode* OwningNode = Pin->LinkedTo[0]->GetOwningNode())
							{
								if (OwningNode == ToNode)
								{
									return true;
								}
								NodesToProcess.Push(OwningNode);
							}
						}
					}
				}
			}
		}

	}

	return false;
}

const FPinConnectionResponse UDataflowSchema::CanCreateConnection(const UEdGraphPin* InPinA, const UEdGraphPin* InPinB) const
{
	bool bSwapped = false;
	const UEdGraphPin* PinA = InPinA;
	const UEdGraphPin* PinB = InPinB;
	if (PinA->Direction == EEdGraphPinDirection::EGPD_Input &&
		PinB->Direction == EEdGraphPinDirection::EGPD_Output)
	{
		bSwapped = true;
		PinA = InPinB; PinB = InPinA;
	}

	if (PinA->Direction == EEdGraphPinDirection::EGPD_Output)
	{
		if (PinB->Direction == EEdGraphPinDirection::EGPD_Input)
		{
			// Make sure the pins are not on the same node
			UDataflowEdNode* EdNodeA = Cast<UDataflowEdNode>(PinA->GetOwningNode());
			UDataflowEdNode* EdNodeB = Cast<UDataflowEdNode>(PinB->GetOwningNode());

			if (EdNodeA && EdNodeB && (EdNodeA != EdNodeB))
			{
				const bool AIsCompatibleWithB = EdNodeA->PinIsCompatibleWithType(*PinA, PinB->PinType);
				const bool BIsCompatibleWithA = EdNodeB->PinIsCompatibleWithType(*PinB, PinA->PinType);
				if (AIsCompatibleWithB || BIsCompatibleWithA)
				{
					// cycle checking on connect
					if (!HasLoopIfConnected(EdNodeA, EdNodeB))
					{
						if (PinB->LinkedTo.Num())
						{
							return (bSwapped) ?
								FPinConnectionResponse(CONNECT_RESPONSE_BREAK_OTHERS_A, LOCTEXT("PinSteal", "Disconnect existing input and connect new input."))
								:
								FPinConnectionResponse(CONNECT_RESPONSE_BREAK_OTHERS_B, LOCTEXT("PinSteal", "Disconnect existing input and connect new input."));

						}

						return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, LOCTEXT("PinConnect", "Connect input to output."));
					}
					else
					{
						return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, LOCTEXT("PinError_Loop", "Graph Cycle"));
					}
				}
				else
				{
					return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, LOCTEXT("PinError_Type mismatch", "Type Mismatch"));
				}
			}
		}
	}
	TArray<FText> NoConnectionResponse = {
		LOCTEXT("PinErrorSameNode_Nope", "Nope"),
		LOCTEXT("PinErrorSameNode_Sorry", "Sorry :("),
		LOCTEXT("PinErrorSameNode_NotGonnaWork", "Not gonna work."),
		LOCTEXT("PinErrorSameNode_StillNo", "Still no!"),
		LOCTEXT("PinErrorSameNode_TryAgain", "Try again?"),
	};
	return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, NoConnectionResponse[FMath::RandRange(0, NoConnectionResponse.Num()-1)]);
}

FLinearColor UDataflowSchema::GetPinTypeColor(const FEdGraphPinType& PinType) const
{
	return GetTypeColor(PinType.PinCategory);
}

FLinearColor UDataflowSchema::GetTypeColor(const FName& Type)
{
	const UGraphEditorSettings* Settings = GetDefault<UGraphEditorSettings>();
	const UDataflowSettings* DataflowSettings = GetDefault<UDataflowSettings>();

	if (Type == UE::Dataflow::Private::ManagedArrayCollectionType)
	{
		return DataflowSettings->ManagedArrayCollectionPinTypeColor;
	}
	else if (Type == UE::Dataflow::Private::FloatType)
	{
		return Settings->FloatPinTypeColor;
	}
	else if (Type == UE::Dataflow::Private::DoubleType)
	{
		return Settings->DoublePinTypeColor;
	}
	else if (Type == UE::Dataflow::Private::Int32Type)
	{
		return Settings->IntPinTypeColor;
	}
	else if (Type == UE::Dataflow::Private::BoolType)
	{
		return Settings->BooleanPinTypeColor;
	}
	else if (Type == UE::Dataflow::Private::StringType)
	{
		return Settings->StringPinTypeColor;
	}
	else if (Type == UE::Dataflow::Private::NameType)
	{
		return Settings->NamePinTypeColor;
	}
	else if (Type == UE::Dataflow::Private::TextType)
	{
		return Settings->TextPinTypeColor;
	}
	else if (Type == UE::Dataflow::Private::VectorType)
	{
		return Settings->VectorPinTypeColor;
	}
	else if (Type == UE::Dataflow::Private::TransformType)
	{
		return Settings->TransformPinTypeColor;
	}
	else if (Type == UE::Dataflow::Private::RotatorType)
	{
		return Settings->RotatorPinTypeColor;
	}
	else if (Type == UE::Dataflow::Private::ArrayType)
	{
		return DataflowSettings->ArrayPinTypeColor;
	}
	else if (Type == UE::Dataflow::Private::BoxType)
	{
		return DataflowSettings->BoxPinTypeColor;
	}
	else if (Type == UE::Dataflow::Private::SphereType)
	{
		return DataflowSettings->SpherePinTypeColor;
	}
	else if (Type == UE::Dataflow::Private::DataflowAnyTypeType)
	{
		return DataflowSettings->DataflowAnyTypePinTypeColor;
	}

	return Settings->DefaultPinTypeColor;
}

static void CreateAndConnectNewReRouteNode(UEdGraphPin* FromPin, UEdGraphPin* ToPin, const FVector2D& GraphPosition)
{
	const UEdGraphNode* FromNode = FromPin->GetOwningNode();
	UEdGraph* EdGraph = FromNode->GetGraph();

	// Add the new reroute node and connect it 
	TSharedPtr<FAssetSchemaAction_Dataflow_CreateNode_DataflowEdNode> NewNodeAction 
		= FAssetSchemaAction_Dataflow_CreateNode_DataflowEdNode::CreateAction(EdGraph, FDataflowReRouteNode::StaticType());
	if (NewNodeAction)
	{
		UEdGraphNode* NewEdNode = NewNodeAction->PerformAction(EdGraph, nullptr, GraphPosition, false);
		if (NewEdNode)
		{
			const FName PinName = "Value";
			UEdGraphPin* InputPin = NewEdNode->FindPin(PinName, EGPD_Input);
			UEdGraphPin* OutputPin = NewEdNode->FindPin(PinName, EGPD_Output);
			if (InputPin && OutputPin)
			{
				EdGraph->GetSchema()->TryCreateConnection(FromPin, InputPin);
				EdGraph->GetSchema()->TryCreateConnection(OutputPin, ToPin);
			}
		}
	}
}


void UDataflowSchema::OnPinConnectionDoubleCicked(UEdGraphPin* PinA, UEdGraphPin * PinB, const FVector2D & GraphPosition) const
{
	CreateAndConnectNewReRouteNode(PinA, PinB, GraphPosition);
}

void UDataflowSchema::BreakPinLinks(UEdGraphPin& TargetPin, bool bSendsNodeNotifcation) const
{
	const FScopedTransaction Transaction(LOCTEXT("BreakPinLinks", "Break Pin Links"));
	Super::BreakPinLinks(TargetPin, bSendsNodeNotifcation);
}

bool UDataflowSchema::TryCreateConnection(UEdGraphPin* PinA, UEdGraphPin* PinB) const
{
	check(PinA && PinB);
	UDataflowEdNode* const DataflowEdNodeA = CastChecked<UDataflowEdNode>(PinA->GetOwningNodeUnchecked());
	UDataflowEdNode* const DataflowEdNodeB = CastChecked<UDataflowEdNode>(PinB->GetOwningNodeUnchecked());
	if (ensure(DataflowEdNodeA->IsBound() && DataflowEdNodeB->IsBound()))
	{
		const TSharedPtr<FDataflowNode> DataflowNodeA = DataflowEdNodeA->GetDataflowNode();
		const TSharedPtr<FDataflowNode> DataflowNodeB = DataflowEdNodeB->GetDataflowNode();
		if (ensure(DataflowNodeA && DataflowNodeB))
		{
			// Pausing invalidations is a quick hack while sorting the invalidation callbacks that are causing multiple evaluations
			DataflowNodeA->PauseInvalidations();
			DataflowNodeB->PauseInvalidations();
			const bool bModified = Super::TryCreateConnection(PinA, PinB);
			DataflowNodeA->ResumeInvalidations();
			DataflowNodeB->ResumeInvalidations();
			return bModified;
		}
	}
	return Super::TryCreateConnection(PinA, PinB);
}

FConnectionDrawingPolicy* UDataflowSchema::CreateConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float InZoomFactor, const FSlateRect& InClippingRect, class FSlateWindowElementList& InDrawElements, class UEdGraph* InGraphObj) const
{
	return new FDataflowConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, InZoomFactor, InClippingRect, InDrawElements, InGraphObj);
}

FDataflowConnectionDrawingPolicy::FDataflowConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float InZoomFactor, const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements, UEdGraph* InGraph)
	: FConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, InZoomFactor, InClippingRect, InDrawElements)
	, Schema((UDataflowSchema*)(InGraph->GetSchema()))
{
	ArrowImage = nullptr;
	ArrowRadius = FVector2D::ZeroVector;
}

void FDataflowConnectionDrawingPolicy::DetermineWiringStyle(UEdGraphPin* OutputPin, UEdGraphPin* InputPin, /*inout*/ FConnectionParams& Params)
{
	FConnectionDrawingPolicy::DetermineWiringStyle(OutputPin, InputPin, Params);
	if (HoveredPins.Contains(InputPin) && HoveredPins.Contains(OutputPin))
	{
		Params.WireThickness = Params.WireThickness * 5;
	}

	const UDataflowSchema* DataflowSchema = GetSchema();
	if (DataflowSchema && OutputPin)
	{
		Params.WireColor = DataflowSchema->GetPinTypeColor(OutputPin->PinType);
	}

	if (OutputPin && InputPin)
	{
		if (OutputPin->bOrphanedPin || InputPin->bOrphanedPin)
		{
			Params.WireColor = FLinearColor::Red;
		}
	}
}

void FDataflowConnectionDrawingPolicy::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Schema);
}


#undef LOCTEXT_NAMESPACE

