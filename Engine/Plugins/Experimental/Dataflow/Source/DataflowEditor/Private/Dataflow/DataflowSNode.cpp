// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowSNode.h"

#include "Dataflow/DataflowEdNode.h"
#include "Dataflow/DataflowEditorStyle.h"
#include "Dataflow/DataflowEngineUtil.h"
#include "Dataflow/DataflowGraphEditor.h"
#include "Dataflow/DataflowNodeFactory.h"
#include "Dataflow/DataflowObject.h"
#include "Framework/Application/SlateApplication.h"
#include "Logging/LogMacros.h"
#include "SourceCodeNavigation.h"
#include "Styling/SlateTypes.h"
#include "Styling/CoreStyle.h"
#include "Styling/AppStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/SBoxPanel.h"
#include "Editor/Transactor.h"
#include "GraphEditorSettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DataflowSNode)

#define LOCTEXT_NAMESPACE "SDataflowEdNode"

//
// SDataflowOutputPin
//

void SDataflowOutputPin::Construct(const FArguments& InArgs, UEdGraphPin* InPin)
{
	const bool bIsPinInvalid = InArgs._IsPinInvalid.Get();
	const FText InvalidPinDisplayText = bIsPinInvalid ? NSLOCTEXT("DataflowGraph", "DataflowOutputPinInvalidText", "*") : NSLOCTEXT("DataflowGraph", "DataflowOutputPinValidText", " ");

	SGraphPin::Construct(SGraphPin::FArguments(), InPin);

	GetLabelAndValue()->AddSlot()
		.Padding(2.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text_Lambda([InvalidPinDisplayText]()
				{
					return InvalidPinDisplayText;
				})
		.MinDesiredWidth(5)
		];
}

//
// SDataflowEdNode
//

void SDataflowEdNode::Construct(const FArguments& InArgs, UDataflowEdNode* InNode)
{
	GraphNode = InNode;
	DataflowGraphNode = Cast<UDataflowEdNode>(InNode);
	DataflowInterface = InArgs._DataflowInterface;

	UpdateGraphNode();

	
	const FSlateBrush* DisabledSwitchBrush = FDataflowEditorStyle::Get().GetBrush(TEXT("Dataflow.Render.Disabled"));
	const FSlateBrush* EnabledSwitchBrush = FDataflowEditorStyle::Get().GetBrush(TEXT("Dataflow.Render.Enabled"));

	//
	// Render
	//
	CheckBoxStyle = FCheckBoxStyle()
		.SetCheckBoxType(ESlateCheckBoxType::CheckBox)
		.SetUncheckedImage(*DisabledSwitchBrush)
		.SetUncheckedHoveredImage(*DisabledSwitchBrush)
		.SetUncheckedPressedImage(*DisabledSwitchBrush)
		.SetCheckedImage(*EnabledSwitchBrush)
		.SetCheckedHoveredImage(*EnabledSwitchBrush)
		.SetCheckedPressedImage(*EnabledSwitchBrush)
		.SetPadding(FMargin(0, 0, 0, 1));
	
	RenderCheckBoxWidget = SNew(SCheckBox)
		.Style(&CheckBoxStyle)
		.IsChecked_Lambda([this]()-> ECheckBoxState
			{
				if (DataflowGraphNode && DataflowGraphNode->ShouldWireframeRenderNode())
				{
					return ECheckBoxState::Checked;
				}
				return ECheckBoxState::Unchecked;
			})
		.OnCheckStateChanged_Lambda([&](const ECheckBoxState NewState)
			{
				if (DataflowGraphNode)
				{
					if (NewState == ECheckBoxState::Checked)
						DataflowGraphNode->SetShouldWireframeRenderNode(true);
					else
						DataflowGraphNode->SetShouldWireframeRenderNode(false);
				}
			})
		.IsEnabled_Lambda([this]()->bool
			{
				if (DataflowGraphNode)
				{
					return DataflowGraphNode->CanEnableWireframeRenderNode();
				}
				return false;
			});


	//
	//  Cached
	//
	/*
	const FSlateBrush* CachedFalseBrush = FDataflowEditorStyle::Get().GetBrush(TEXT("Dataflow.Cached.False"));
	const FSlateBrush* CachedTrueBrush = FDataflowEditorStyle::Get().GetBrush(TEXT("Dataflow.Cached.True"));

	CacheStatusStyle = FCheckBoxStyle()
		.SetCheckBoxType(ESlateCheckBoxType::CheckBox)
		.SetUncheckedImage(*CachedFalseBrush)
		.SetUncheckedHoveredImage(*CachedFalseBrush)
		.SetUncheckedPressedImage(*CachedFalseBrush)
		.SetCheckedImage(*CachedTrueBrush)
		.SetCheckedHoveredImage(*CachedTrueBrush)
		.SetCheckedPressedImage(*CachedTrueBrush)
		.SetPadding(FMargin(0, 0, 0, 1));

		CacheStatus = SNew(SCheckBox)
		.Style(&CacheStatusStyle)
		.IsChecked_Lambda([this]()-> ECheckBoxState
		{
			if (DataflowGraphNode)
			{
				return ECheckBoxState::Checked;
				// @todo(dataflow) : Missing connection to FToolkit
			}
			return ECheckBoxState::Unchecked;
		});
	*/
}

TSharedPtr<SGraphPin> SDataflowEdNode::CreatePinWidget(UEdGraphPin* Pin) const
{
	if (Pin->Direction == EEdGraphPinDirection::EGPD_Output)
	{
		if (DataflowGraphNode)
		{
			if (TSharedPtr<FDataflowNode> DataflowNode = DataflowGraphNode->GetDataflowNode())
			{
				if (FDataflowOutput* Output = DataflowNode->FindOutput(Pin->GetFName()))
				{
					if (const TSharedPtr<UE::Dataflow::FContext> DataflowContext = DataflowInterface->GetDataflowContext())
					{
						TSet<UE::Dataflow::FContextCacheKey> CacheKeys;
						const int32 NumKeys = DataflowContext->GetKeys(CacheKeys);

						//
						// DataStore is empty or 
						// CacheKey is not in DataStore or
						// Node's Timestamp is invalid or
						// Node's Timestamp is greater than CacheKey's Timestamp -> Pin is invalid
						//
						const bool bIsOutputInvalid = !NumKeys ||
							!CacheKeys.Contains(Output->CacheKey()) ||
PRAGMA_DISABLE_DEPRECATION_WARNINGS  // Until LastModifiedTimestamp becomes private
							DataflowNode->LastModifiedTimestamp.IsInvalid() || 
							!DataflowContext->IsCacheEntryAfterTimestamp(Output->CacheKey(), DataflowNode->LastModifiedTimestamp);
PRAGMA_ENABLE_DEPRECATION_WARNINGS

						return SNew(SDataflowOutputPin, Pin)
								.IsPinInvalid(bIsOutputInvalid);
					}
				}
			}
		}
	}

	return SGraphNode::CreatePinWidget(Pin);
}

TArray<FOverlayWidgetInfo> SDataflowEdNode::GetOverlayWidgets(bool bSelected, const FVector2D& WidgetSize) const
{
	TArray<FOverlayWidgetInfo> Widgets = SGraphNode::GetOverlayWidgets(bSelected, WidgetSize);

	if (DataflowGraphNode && DataflowInterface->NodesHaveToggleWidget() && DataflowGraphNode->GetDataflowNode() && DataflowGraphNode->GetDataflowNode()->GetRenderParameters().Num())
	{
		const FVector2D ImageSize = RenderCheckBoxWidget->GetDesiredSize();

		FOverlayWidgetInfo Info;
		Info.OverlayOffset = FVector2D(WidgetSize.X - ImageSize.X - 6.f, 6.f);
		Info.Widget = RenderCheckBoxWidget;

		Widgets.Add(Info);
	}

	/*
	if (DataflowGraphNode )
	{
		// @todo(dataflow) : Need to bump the title box over 6px
		const FVector2D ImageSize = CacheStatus->GetDesiredSize();

		FOverlayWidgetInfo Info;
		Info.OverlayOffset = FVector2D(6.f, 6.f);
		Info.Widget = CacheStatus;

		Widgets.Add(Info);
	}
	*/
	return Widgets;
}

void SDataflowEdNode::UpdateErrorInfo()
{
	if (DataflowGraphNode)
	{
		if (const TSharedPtr<FDataflowNode> DataflowNode = DataflowGraphNode->GetDataflowNode())
		{
			if (UE::Dataflow::FNodeFactory::IsNodeExperimental(DataflowNode->GetType()))
			{
				ErrorMsg = FString(TEXT("Experimental"));
				ErrorColor = FAppStyle::GetColor("ErrorReporting.WarningBackgroundColor");
			} 
			if (UE::Dataflow::FNodeFactory::IsNodeDeprecated(DataflowNode->GetType()))
			{
				ErrorMsg = FString(TEXT("Deprecated"));
				ErrorColor = FAppStyle::GetColor("ErrorReporting.WarningBackgroundColor");
			}
		}
	}
	
}

FReply SDataflowEdNode::OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent)
{
	if (GraphNode)
	{
		if (UDataflowEdNode* DataflowNode = Cast<UDataflowEdNode>(GraphNode))
		{
			if (TSharedPtr<UE::Dataflow::FGraph> Graph = DataflowNode->GetDataflowGraph())
			{
				if (TSharedPtr<FDataflowNode> Node = Graph->FindBaseNode(DataflowNode->GetDataflowNodeGuid()))
				{
					if (FSourceCodeNavigation::CanNavigateToStruct(Node->TypedScriptStruct()))
					{
						FSourceCodeNavigation::NavigateToStruct(Node->TypedScriptStruct());
					}
				}
			}
		}
	}
	return Super::OnMouseButtonDoubleClick(InMyGeometry, InMouseEvent);
}

void SDataflowEdNode::AddReferencedObjects(FReferenceCollector& Collector)
{
	if (DataflowGraphNode)
	{
		Collector.AddReferencedObject(DataflowGraphNode);
		if (TSharedPtr<FDataflowNode> DataflowNode = DataflowGraphNode->GetDataflowNode())
		{
			Collector.AddPropertyReferences(DataflowNode->TypedScriptStruct(), DataflowNode.Get());
		}
	}
}

void SDataflowEdNode::CreateInputSideAddButton(TSharedPtr<SVerticalBox> InputBox)
{
	TSharedRef<SWidget> AddPinButton = AddPinButtonContent(
		LOCTEXT("AddPinInputButton", "Show/Hide Inputs"),
		LOCTEXT("AddPinInputButton_Tooltip", "Show/Hide input pins."),
		false
	);

	FMargin AddPinPadding = Settings->GetOutputPinPadding();
	AddPinPadding.Top += 6.0f;

	InputBox->AddSlot()
		.AutoHeight()
		.VAlign(VAlign_Center)
		.Padding(AddPinPadding)
		[
			AddPinButton
		];
}

FReply SDataflowEdNode::OnAddPin()
{
	if (DataflowGraphNode)
	{
		FMenuBuilder MenuBuilder(false, nullptr);
		if (TSharedPtr<FDataflowNode> DataflowNode = DataflowGraphNode->GetDataflowNode())
		{
			if (DataflowNode->HasHideableInputs())
			{
				MenuBuilder.AddMenuEntry(LOCTEXT("HideAllInputs", "Hide all"), LOCTEXT("HideAllInputsTooltip", "Hide all hideable input pins"), FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateUObject(DataflowGraphNode, &UDataflowEdNode::HideAllInputPins)));
				MenuBuilder.AddMenuEntry(LOCTEXT("UnhideAllInputs", "Show all"), LOCTEXT("UnhideAllInputsTooltip", "Show all hideable input pins"), FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateUObject(DataflowGraphNode, &UDataflowEdNode::ShowAllInputPins)));


				TArray<FDataflowInput*> Inputs = DataflowNode->GetInputs();
				for (FDataflowInput* Input : Inputs)
				{
					if (Input->GetCanHidePin())
					{
						MenuBuilder.AddMenuEntry(FText::FromName(Input->GetName()), LOCTEXT("UnhidePinTooltip", "Show/Hide pin"), FSlateIcon(),
							FUIAction(
								FExecuteAction::CreateUObject(DataflowGraphNode, &UDataflowEdNode::ToggleHideInputPin, Input->GetName()),
								FCanExecuteAction::CreateUObject(DataflowGraphNode, &UDataflowEdNode::CanToggleHideInputPin, Input->GetName()),
								FIsActionChecked::CreateUObject(DataflowGraphNode, &UDataflowEdNode::IsInputPinShown, Input->GetName())),
							NAME_None, EUserInterfaceActionType::ToggleButton);
					}
				}
			}
		}
		FSlateApplication::Get().PushMenu(AsShared(),
			FWidgetPath(),
			MenuBuilder.MakeWidget(),
			FSlateApplication::Get().GetCursorPos(),
			FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu)
		);
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

EVisibility SDataflowEdNode::IsAddPinButtonVisible() const
{
	EVisibility Visibility = Super::IsAddPinButtonVisible();
	if (Visibility == EVisibility::Collapsed)
	{
		return Visibility;
	}

	if (DataflowGraphNode)
	{
		if (const TSharedPtr<FDataflowNode> DataflowNode = DataflowGraphNode->GetDataflowNode())
		{
			if (DataflowNode->HasHideableInputs())
			{
				return Visibility;
			}
		}
	}

	return EVisibility::Collapsed;
}

//
// Add a menu option to create a graph node.
//
TSharedPtr<FAssetSchemaAction_Dataflow_CreateNode_DataflowEdNode> FAssetSchemaAction_Dataflow_CreateNode_DataflowEdNode::CreateAction(const UEdGraph* ParentGraph, const FName & InNodeTypeName, const FName& InOverrideNodeName)
{
	if (const UDataflow* Dataflow = Cast<UDataflow>(ParentGraph))
	{
		if (UE::Dataflow::FNodeFactory* Factory = UE::Dataflow::FNodeFactory::GetInstance())
		{
			const UE::Dataflow::FFactoryParameters& Param = Factory->GetParameters(InNodeTypeName);
			if (Param.IsValid())
			{
				const bool bIsSimulationNode = Param.Tags.Contains(UDataflow::SimulationTag);
				const bool bIsSimulationGraph = (Dataflow->Type == EDataflowType::Simulation);
				
				if((bIsSimulationGraph && bIsSimulationNode) || (!bIsSimulationGraph && !bIsSimulationNode))
				{
					const FText ToolTip = FText::FromString(Param.ToolTip.IsEmpty() ? FString("Add a Dataflow node.") : Param.ToolTip);
					FText NodeName = FText::FromString(Param.DisplayName.ToString());
					if (!InOverrideNodeName.IsNone())
					{
						NodeName = FText::FromName(InOverrideNodeName);
					}
				
					const FText Category = FText::FromString(Param.Category.ToString().IsEmpty() ? FString("Dataflow") : Param.Category.ToString());
					const FText Tags = FText::FromString(Param.Tags);
					TSharedPtr<FAssetSchemaAction_Dataflow_CreateNode_DataflowEdNode> NewNodeAction(
						new FAssetSchemaAction_Dataflow_CreateNode_DataflowEdNode(InNodeTypeName, Category, NodeName, ToolTip, Tags));
					return NewNodeAction;
				}
			}
		}
	}
	return TSharedPtr<FAssetSchemaAction_Dataflow_CreateNode_DataflowEdNode>(nullptr);
}

static FName GetNodeUniqueName(UDataflow* Dataflow, FString NodeBaseName)
{
	FString Left, Right;
	int32 NameIndex = 1;

	// Check if NodeBaseName already ends with "_dd"
	if (NodeBaseName.Split(TEXT("_"), &Left, &Right, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
	{
		if (Right.IsNumeric())
		{
			NameIndex = FCString::Atoi(*Right);

			NodeBaseName = Left;
		}
	}

	FName NodeUniqueName{ NodeBaseName };
	while (Dataflow->GetDataflow()->FindBaseNode(FName(NodeUniqueName)) != nullptr)
	{
		NodeUniqueName = FName(NodeBaseName + FString::Printf(TEXT("_%02d"), NameIndex));
		NameIndex++;
	}

	return NodeUniqueName;
}

void SDataflowEdNode::CopyDataflowNodeSettings(TSharedPtr<FDataflowNode> SourceDataflowNode, TSharedPtr<FDataflowNode> TargetDataflowNode)
{
	using namespace UE::Transaction;
	FSerializedObject SerializationObject;

	FSerializedObjectDataWriter ArWriter(SerializationObject);
	SourceDataflowNode->SerializeInternal(ArWriter);

	FSerializedObjectDataReader ArReader(SerializationObject);
	TargetDataflowNode->SerializeInternal(ArReader);
}

static UDataflowEdNode* CreateNode(UDataflow* Dataflow, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode, const FName NodeUniqueName, const FName NodeTypeName, TSharedPtr<FDataflowNode> DataflowNodeToDuplicate, bool bCopySettings = false)
{
	if (UE::Dataflow::FNodeFactory* Factory = UE::Dataflow::FNodeFactory::GetInstance())
	{
		if (TSharedPtr<FDataflowNode> DataflowNode =
			Factory->NewNodeFromRegisteredType(
				*Dataflow->GetDataflow(),
				{ FGuid::NewGuid(), NodeTypeName, NodeUniqueName, Dataflow }))
		{
			if (UDataflowEdNode* EdNode = NewObject<UDataflowEdNode>(Dataflow, UDataflowEdNode::StaticClass(), NodeUniqueName))
			{
				Dataflow->Modify();
				if (FromPin != nullptr)
				{
					FromPin->Modify();
				}

				Dataflow->AddNode(EdNode, true, bSelectNewNode);

				// Copy properties from DataflowNodeToDuplicate to DataflowNode
				if (bCopySettings)
				{
					SDataflowEdNode::CopyDataflowNodeSettings(DataflowNodeToDuplicate, DataflowNode);
				}
				
				EdNode->CreateNewGuid();
				EdNode->PostPlacedNewNode();

				EdNode->SetDataflowGraph(Dataflow->GetDataflow());
				EdNode->SetDataflowNodeGuid(DataflowNode->GetGuid());
				EdNode->AllocateDefaultPins();

				EdNode->AutowireNewNode(FromPin);

				EdNode->NodePosX = Location.X;
				EdNode->NodePosY = Location.Y;

				EdNode->SetFlags(RF_Transactional);

				return EdNode;
			}
		}
	}

	return nullptr;
}

//
//  Created the EdGraph node and bind the guids to the Dataflow's node. 
//
UEdGraphNode* FAssetSchemaAction_Dataflow_CreateNode_DataflowEdNode::PerformAction(class UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode)
{
	if (UDataflow* Dataflow = Cast<UDataflow>(ParentGraph))
	{
		// by default use the type name and check if it is unique in the context of the graph
		// if not, then generate a unique name 
		const FString NodeBaseName = GetMenuDescription().ToString();
		FName NodeUniqueName{ NodeBaseName };
		int32 NameIndex= 0;
		while (Dataflow->GetDataflow()->FindBaseNode(FName(NodeUniqueName)) != nullptr)
		{ 
			NodeUniqueName = FName(NodeBaseName + FString::Printf(TEXT("_%d"), NameIndex));
			NameIndex++;
		}

		return CreateNode(Dataflow, FromPin, Location, bSelectNewNode, NodeUniqueName, NodeTypeName, nullptr);
	}

	return nullptr;
}

//void FAssetSchemaAction_Dataflow_CreateNode_DataflowEdNode::AddReferencedObjects(FReferenceCollector& Collector)
//{
//	FEdGraphSchemaAction::AddReferencedObjects(Collector);
//	Collector.AddReferencedObject(NodeTemplate);
//}

//
// 
//
TSharedPtr<FAssetSchemaAction_Dataflow_DuplicateNode_DataflowEdNode> FAssetSchemaAction_Dataflow_DuplicateNode_DataflowEdNode::CreateAction(UEdGraph* ParentGraph, const FName& InNodeTypeName)
{
	if (UE::Dataflow::FNodeFactory* Factory = UE::Dataflow::FNodeFactory::GetInstance())
	{
		const UE::Dataflow::FFactoryParameters& Param = Factory->GetParameters(InNodeTypeName);
		if (Param.IsValid())
		{
			const FText ToolTip = FText::FromString(Param.ToolTip.IsEmpty() ? FString("Add a Dataflow node.") : Param.ToolTip);
			const FText NodeName = FText::FromString(Param.DisplayName.ToString());
			const FText Category = FText::FromString(Param.Category.ToString().IsEmpty() ? FString("Dataflow") : Param.Category.ToString());
			const FText Tags = FText::FromString(Param.Tags);
			TSharedPtr<FAssetSchemaAction_Dataflow_DuplicateNode_DataflowEdNode> NewNodeAction(
				new FAssetSchemaAction_Dataflow_DuplicateNode_DataflowEdNode(InNodeTypeName, Category, NodeName, ToolTip, Tags));
			return NewNodeAction;
		}
	}
	return TSharedPtr<FAssetSchemaAction_Dataflow_DuplicateNode_DataflowEdNode>(nullptr);
}

//
//   
//
UEdGraphNode* FAssetSchemaAction_Dataflow_DuplicateNode_DataflowEdNode::PerformAction(class UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode)
{
	if (UDataflow* Dataflow = Cast<UDataflow>(ParentGraph))
	{
		FString NodeToDuplicateName = DataflowNodeToDuplicate->GetName().ToString();

		// Check if that is unique, if not then make it unique with an index postfix
		FName NodeUniqueName = GetNodeUniqueName(Dataflow, NodeToDuplicateName);

		return CreateNode(Dataflow, FromPin, Location, bSelectNewNode, NodeUniqueName, NodeTypeName, DataflowNodeToDuplicate, /*bCopySettings=*/true);
	}

	return nullptr;
}

//
// 
//
TSharedPtr<FAssetSchemaAction_Dataflow_PasteNode_DataflowEdNode> FAssetSchemaAction_Dataflow_PasteNode_DataflowEdNode::CreateAction(UEdGraph* ParentGraph, const FName& InNodeTypeName)
{
	if (UE::Dataflow::FNodeFactory* Factory = UE::Dataflow::FNodeFactory::GetInstance())
	{
		const UE::Dataflow::FFactoryParameters& Param = Factory->GetParameters(InNodeTypeName);
		if (Param.IsValid())
		{
			const FText ToolTip = FText::FromString(Param.ToolTip.IsEmpty() ? FString("Add a Dataflow node.") : Param.ToolTip);
			const FText NodeName = FText::FromString(Param.DisplayName.ToString());
			const FText Category = FText::FromString(Param.Category.ToString().IsEmpty() ? FString("Dataflow") : Param.Category.ToString());
			const FText Tags = FText::FromString(Param.Tags);
			TSharedPtr<FAssetSchemaAction_Dataflow_PasteNode_DataflowEdNode> NewNodeAction(
				new FAssetSchemaAction_Dataflow_PasteNode_DataflowEdNode(InNodeTypeName, Category, NodeName, ToolTip, Tags));
			return NewNodeAction;
		}
	}
	return TSharedPtr<FAssetSchemaAction_Dataflow_PasteNode_DataflowEdNode>(nullptr);
}

static UDataflowEdNode* CreateNodeFromPaste(UDataflow* Dataflow, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode, const FName NodeUniqueName, const FName NodeTypeName, FString NodeProperties)
{
	if (UE::Dataflow::FNodeFactory* Factory = UE::Dataflow::FNodeFactory::GetInstance())
	{
		if (TSharedPtr<FDataflowNode> DataflowNode =
			Factory->NewNodeFromRegisteredType(
				*Dataflow->GetDataflow(),
				{ FGuid::NewGuid(), NodeTypeName, NodeUniqueName, Dataflow }))
		{
			if (UDataflowEdNode* EdNode = NewObject<UDataflowEdNode>(Dataflow, UDataflowEdNode::StaticClass(), NodeUniqueName))
			{
				Dataflow->Modify();

				Dataflow->AddNode(EdNode, true, bSelectNewNode);

				// Copy properties to DataflowNode
				if (!NodeProperties.IsEmpty())
				{
					DataflowNode->TypedScriptStruct()->ImportText(*NodeProperties, DataflowNode.Get(), nullptr, EPropertyPortFlags::PPF_None, nullptr, DataflowNode->TypedScriptStruct()->GetName(), true);
				}
				// Do any post-import fixup.
				FArchive Ar;
				Ar.SetIsLoading(true);
				DataflowNode->PostSerialize(Ar);

				EdNode->CreateNewGuid();
				EdNode->PostPlacedNewNode();

				EdNode->SetDataflowGraph(Dataflow->GetDataflow());
				EdNode->SetDataflowNodeGuid(DataflowNode->GetGuid());
				EdNode->AllocateDefaultPins();

				EdNode->NodePosX = Location.X;
				EdNode->NodePosY = Location.Y;

				EdNode->SetFlags(RF_Transactional);

				return EdNode;
			}
		}
	}

	return nullptr;
}

//
//   
//
UEdGraphNode* FAssetSchemaAction_Dataflow_PasteNode_DataflowEdNode::PerformAction(class UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode)
{
	if (UDataflow* Dataflow = Cast<UDataflow>(ParentGraph))
	{
		FString NodeToDuplicateName = NodeName.ToString();

		// Check if that is unique, if not then make it unique with an index postfix
		FName NodeUniqueName = GetNodeUniqueName(Dataflow, NodeToDuplicateName);

		return CreateNodeFromPaste(Dataflow, FromPin, Location, bSelectNewNode, NodeUniqueName, NodeTypeName, NodeProperties);
	}

	return nullptr;
}
#undef LOCTEXT_NAMESPACE

