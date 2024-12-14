// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowEdNode.h"

#include "Dataflow/DataflowNode.h"
#include "Dataflow/DataflowCore.h"
#include "Dataflow/DataflowCoreNodes.h"
#include "Dataflow/DataflowObject.h"
#include "GeometryCollection/Facades/CollectionRenderingFacade.h"
#include "Logging/LogMacros.h"
#include "Textures/SlateIcon.h"
#include "Styling/AppStyle.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DataflowEdNode)

#if WITH_EDITOR
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Commands/UIAction.h"
#endif

#define LOCTEXT_NAMESPACE "DataflowEdNode"

DEFINE_LOG_CATEGORY_STATIC(DATAFLOWNODE_LOG, Error, All);

namespace UE::Dataflow::Private
{
static UE::Dataflow::FPin::EDirection EdPinDirectionToDataflowDirection(EEdGraphPinDirection EdDirection)
{
	if (EdDirection == EEdGraphPinDirection::EGPD_Input)
	{
		return FPin::EDirection::INPUT;
	}
	if (EdDirection == EEdGraphPinDirection::EGPD_Output)
	{
		return FPin::EDirection::OUTPUT;
	}
	return FPin::EDirection::NONE;
}

static EEdGraphPinDirection DataflowDirectionToEdPinDirection(UE::Dataflow::FPin::EDirection Direction)
{
	if (Direction == FPin::EDirection::INPUT)
	{
		return EEdGraphPinDirection::EGPD_Input;
	}
	if (Direction == FPin::EDirection::OUTPUT)
	{
		return EEdGraphPinDirection::EGPD_Output;
	}
	return EEdGraphPinDirection::EGPD_MAX;
}
}

UDataflowEdNode::UDataflowEdNode(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITOR && !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	bCanRenameNode = true;
#endif // WITH_EDITOR && !UE_BUILD_SHIPPING
}

void UDataflowEdNode::SetShouldRenderNode(bool bInRender)
{
	bRenderInAssetEditor = bInRender;
	if (IsBound())
	{
#if WITH_EDITOR && !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		if (UDataflow* DataflowObject = Cast< UDataflow>(GetGraph()))
		{
			if (bRenderInAssetEditor)
			{
				DataflowObject->AddRenderTarget(this);
			}
			else
			{
				DataflowObject->RemoveRenderTarget(this);
			}
		}
#endif
	}
}

void UDataflowEdNode::SetShouldWireframeRenderNode(bool bInRender)
{
	bRenderWireframeInAssetEditor = bInRender;
	if (IsBound())
	{
#if WITH_EDITOR && !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		if (UDataflow* const DataflowObject = Cast<UDataflow>(GetGraph()))
		{
			if (bRenderWireframeInAssetEditor)
			{
				DataflowObject->AddWireframeRenderTarget(this);
			}
			else
			{
				DataflowObject->RemoveWireframeRenderTarget(this);
			}
		}
#endif
	}
}

void UDataflowEdNode::SetCanEnableWireframeRenderNode(bool bInCanEnable)
{
	bCanEnableRenderWireframe = bInCanEnable;
}

bool UDataflowEdNode::CanEnableWireframeRenderNode() const
{
	return bCanEnableRenderWireframe;
}

TSharedPtr<FDataflowNode> UDataflowEdNode::GetDataflowNode()
{
	if(TSharedPtr<UE::Dataflow::FGraph> Dataflow = GetDataflowGraph())
	{
		return Dataflow->FindBaseNode(GetDataflowNodeGuid());
	}
	return TSharedPtr<FDataflowNode>(nullptr);
}

TSharedPtr<const FDataflowNode> UDataflowEdNode::GetDataflowNode() const
{
	if (TSharedPtr<const UE::Dataflow::FGraph> Dataflow = GetDataflowGraph())
	{
		return Dataflow->FindBaseNode(GetDataflowNodeGuid());
	}
	return TSharedPtr<FDataflowNode>(nullptr);
}


void UDataflowEdNode::AllocateDefaultPins()
{
	UE_LOG(DATAFLOWNODE_LOG, Verbose, TEXT("UDataflowEdNode::AllocateDefaultPins()"));
	// called on node creation from UI. 

#if WITH_EDITOR && !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (DataflowGraph)
	{
		if (DataflowNodeGuid.IsValid())
		{
			if (TSharedPtr<FDataflowNode> DataflowNode = DataflowGraph->FindBaseNode(DataflowNodeGuid))
			{
				for (const UE::Dataflow::FPin& Pin : DataflowNode->GetPins())
				{
					UEdGraphPin* const EdPin = CreatePin(UE::Dataflow::Private::DataflowDirectionToEdPinDirection(Pin.Direction), Pin.Type, Pin.Name);
					EdPin->bHidden = Pin.bHidden;
				}
			}
		}
	}
#endif // WITH_EDITOR && !UE_BUILD_SHIPPING
}

void UDataflowEdNode::UpdatePinsFromDataflowNode()
{
	UE_LOG(DATAFLOWNODE_LOG, Verbose, TEXT("UDataflowEdNode::UpdatePinsFromDataflowNode()"));
	// called on node creation from UI. 

#if WITH_EDITOR && !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (DataflowGraph)
	{
		if (DataflowNodeGuid.IsValid())
		{
			if (TSharedPtr<FDataflowNode> DataflowNode = DataflowGraph->FindBaseNode(DataflowNodeGuid))
			{
				// remove pins that do not match inputs / outputs anymore
				TArray<UEdGraphPin*> PinsToRemove;
				for (UEdGraphPin* Pin : GetAllPins())
				{
					if (Pin)
					{
						if (Pin->Direction == EEdGraphPinDirection::EGPD_Input)
						{
							const FDataflowInput* DataflowInput = DataflowNode->FindInput(Pin->GetFName());
							if (!DataflowInput)
							{
								PinsToRemove.Add(Pin);
							}
							else if (DataflowInput->GetType() != Pin->PinType.PinCategory)
							{
								Pin->PinType = FEdGraphPinType();
								Pin->PinType.bIsReference = false;
								Pin->PinType.bIsConst = false;
								Pin->PinType.PinCategory = DataflowInput->GetType();
								Pin->PinType.PinSubCategory = NAME_None;
								Pin->PinType.PinSubCategoryObject = nullptr;
							}
						}
						else if (Pin->Direction == EEdGraphPinDirection::EGPD_Output)
						{
							const FDataflowOutput* DataflowOutput = DataflowNode->FindOutput(Pin->GetFName());
							if (!DataflowOutput)
							{
								PinsToRemove.Add(Pin);
							}
							else if (DataflowOutput->GetType() != Pin->PinType.PinCategory)
							{
								Pin->PinType = FEdGraphPinType();
								Pin->PinType.bIsReference = false;
								Pin->PinType.bIsConst = false;
								Pin->PinType.PinCategory = DataflowOutput->GetType();
								Pin->PinType.PinSubCategory = NAME_None;
								Pin->PinType.PinSubCategoryObject = nullptr;
							}
						}
					}
				}
				for (UEdGraphPin* PinToRemove : PinsToRemove)
				{
					RemovePin(PinToRemove);
				}
				PinsToRemove.Reset();

				for (const UE::Dataflow::FPin& Pin : DataflowNode->GetPins())
				{
					const EEdGraphPinDirection EdDirection = UE::Dataflow::Private::DataflowDirectionToEdPinDirection(Pin.Direction);
					UEdGraphPin* EdPin = FindPin(Pin.Name, EdDirection);
					if (!EdPin)
					{
						EdPin = CreatePin(EdDirection, Pin.Type, Pin.Name);
					}
					EdPin->bHidden = Pin.bHidden;
				}
			}
		}
	}
#endif // WITH_EDITOR && !UE_BUILD_SHIPPING
}

void UDataflowEdNode::AddOptionPin()
{
#if WITH_EDITOR && !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (DataflowGraph && DataflowNodeGuid.IsValid())
	{
		// Modify();  // TODO: How do we modify a DataflowNode

		if (const TSharedPtr<FDataflowNode> DataflowNode = DataflowGraph->FindBaseNode(DataflowNodeGuid))
		{
			const TArray<UE::Dataflow::FPin> AddedPins = DataflowNode->AddPins();
			for (const UE::Dataflow::FPin& Pin : AddedPins)
			{
				switch (Pin.Direction)
				{
				case UE::Dataflow::FPin::EDirection::INPUT:
				case UE::Dataflow::FPin::EDirection::OUTPUT:
					CreatePin(UE::Dataflow::Private::DataflowDirectionToEdPinDirection(Pin.Direction), Pin.Type, Pin.Name);
					ReconstructNode();
					break;
				default:
					break;  // Add pin isn't implemented on this node
				}
			}
		}

		// Refresh the current graph, so the pins can be updated
		if (UEdGraph* const ParentGraph = GetGraph())
		{
			ParentGraph->NotifyGraphChanged();
		}
	}
#endif // WITH_EDITOR && !UE_BUILD_SHIPPING
}

void UDataflowEdNode::RemoveOptionPin()
{
#if WITH_EDITOR && !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (DataflowGraph && DataflowNodeGuid.IsValid())
	{
		// Modify();  // TODO: How do we modify a DataflowNode

		if (const TSharedPtr<FDataflowNode> DataflowNode = DataflowGraph->FindBaseNode(DataflowNodeGuid))
		{
			const TArray<UE::Dataflow::FPin> RemovePins = DataflowNode->GetPinsToRemove();
			UE::Dataflow::FDataflowNodePauseInvalidationScope PauseInvalidationScope(DataflowNode.Get()); // Don't call invalidations per pin. Nodes may not evaluate correctly until all pins have been removed.
			for (const UE::Dataflow::FPin& Pin : RemovePins)
			{
				switch (Pin.Direction)
				{
				case UE::Dataflow::FPin::EDirection::INPUT:
				case UE::Dataflow::FPin::EDirection::OUTPUT:
					if (UEdGraphPin* const EdPin = FindPin(Pin.Name, UE::Dataflow::Private::DataflowDirectionToEdPinDirection(Pin.Direction)))
					{
						constexpr bool bNotifyNodes = true;
						EdPin->BreakAllPinLinks(bNotifyNodes);
						RemovePin(EdPin);
						ReconstructNode();
					}
					break;
				default:
					break;  // Add pin isn't implemented on this node
				}
			}
		}

		// Refresh the current graph, so the pins can be updated
		if (UEdGraph* const ParentGraph = GetGraph())
		{
			ParentGraph->NotifyGraphChanged();
		}
	}
#endif // WITH_EDITOR && !UE_BUILD_SHIPPING
}

bool UDataflowEdNode::PinIsCompatibleWithType(const UEdGraphPin& Pin, const FEdGraphPinType& PinType) const
{
#if WITH_EDITOR
	check(Pin.GetOwningNode() == this);
	if (TSharedPtr<const FDataflowNode> DataflowNode = GetDataflowNode())
	{
		if (Pin.Direction == EEdGraphPinDirection::EGPD_Input)
		{
			return DataflowNode->InputSupportsType(Pin.GetFName(), PinType.PinCategory);
		}
		if (Pin.Direction == EEdGraphPinDirection::EGPD_Output)
		{
			return DataflowNode->OutputSupportsType(Pin.GetFName(), PinType.PinCategory);
		}
	}
#endif // WITH_EDITOR
	return false;
}

FText UDataflowEdNode::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return FText::FromString(GetName());
}

#if WITH_EDITOR && !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
void UDataflowEdNode::PinConnectionListChanged(UEdGraphPin* Pin)
{
	if (ensure(IsBound()))
	{
		if (TSharedPtr<FDataflowNode> DataflowNode = DataflowGraph->FindBaseNode(DataflowNodeGuid))
		{
			check(Pin);

			// Return whether a pin in the UEdGraph matches the specified connection in the Dataflow::FGraph
			auto MatchesConnection = [this](const UEdGraphPin* const Pin, const FDataflowConnection* const Connection) -> bool
				{
					if (const UDataflowEdNode* const LinkedNode = Cast<UDataflowEdNode>(Pin->GetOwningNode()))
					{
						if (ensure(LinkedNode->IsBound()))
						{
							if (const TSharedPtr<FDataflowNode> LinkedDataflowNode = DataflowGraph->FindBaseNode(LinkedNode->GetDataflowNodeGuid()))
							{
								return
									(Pin->Direction == EEdGraphPinDirection::EGPD_Input &&
										Connection == LinkedDataflowNode->FindInput(FName(Pin->GetName()))) ||
									(Pin->Direction == EEdGraphPinDirection::EGPD_Output &&
										Connection == LinkedDataflowNode->FindOutput(FName(Pin->GetName())));
							}
						}
					}
					return false;
				};

			if (Pin->Direction == EEdGraphPinDirection::EGPD_Input)
			{
				if (FDataflowInput* const ConnectionInput = DataflowNode->FindInput(FName(Pin->GetName())))
				{
					// Add any newly added connections
					for (const UEdGraphPin* const LinkedCon : Pin->LinkedTo)
					{
						const UDataflowEdNode* const LinkedNode = Cast<UDataflowEdNode>(LinkedCon->GetOwningNode());
						if (ensure(LinkedNode && LinkedNode->IsBound()))
						{
							if (const TSharedPtr<FDataflowNode> LinkedDataflowNode = DataflowGraph->FindBaseNode(LinkedNode->GetDataflowNodeGuid()))
							{
								if (FDataflowOutput* const LinkedConOutput = LinkedDataflowNode->FindOutput(FName(LinkedCon->GetName())))
								{
									if (ConnectionInput->GetConnectedOutputs().Find(LinkedConOutput) == INDEX_NONE)
									{
										const bool bTypeChanged = DataflowNode->TrySetConnectionType(ConnectionInput, LinkedConOutput->GetType());
										DataflowGraph->Connect(LinkedConOutput, ConnectionInput);

										if (bTypeChanged)
										{
											UpdatePinsFromDataflowNode();
										}
									}
								}
							}
						}
					}
					// Clear any defunct connection
					if (FDataflowOutput* const ConnectedOutput = ConnectionInput->GetConnection())
					{
						if (!Pin->LinkedTo.FindByPredicate(
							[this, ConnectedOutput, &MatchesConnection](const UEdGraphPin* const LinkedCon) -> bool
							{
								return MatchesConnection(LinkedCon, ConnectedOutput);
							}))
						{
							DataflowGraph->Disconnect(ConnectedOutput, ConnectionInput);
						}
					}
				}
			}
			else if (Pin->Direction == EEdGraphPinDirection::EGPD_Output)
			{
				// Update newly added connections from the input pins
				for (UEdGraphPin* const LinkedPin : Pin->LinkedTo)
				{
					LinkedPin->GetOwningNode()->PinConnectionListChanged(LinkedPin);
				}

				// Remove any remaining defunct connections
				if (FDataflowOutput* const ConnectionOutput = DataflowNode->FindOutput(FName(Pin->GetName())))
				{
					const TArray<FDataflowInput*> InputsToDisconnect =
						ConnectionOutput->GetConnections().FilterByPredicate(
							[this, &Pin, &MatchesConnection](FDataflowInput* const ConnectedInput) -> bool
							{
								return ensure(ConnectedInput) && !Pin->LinkedTo.FindByPredicate(
									[this, ConnectedInput, &MatchesConnection](const UEdGraphPin* const LinkedCon) -> bool
									{
										return MatchesConnection(LinkedCon, ConnectedInput);
									});
							});
					UDataflow* const EdGraph = CastChecked<UDataflow>(GetGraph());
					for (FDataflowInput* const ConnectedInput : InputsToDisconnect)
					{
						const TObjectPtr<UDataflowEdNode> InputEdNode = EdGraph->FindEdNodeByDataflowNodeGuid(ConnectedInput->GetOwningNodeGuid());
						if (ensure(InputEdNode))
						{
							UEdGraphPin* const InputPin = InputEdNode->FindPin(ConnectedInput->GetName(), EEdGraphPinDirection::EGPD_Input);
							if (ensure(InputPin))
							{
								// To avoid double invalidations, instead of disconnecting, update connections by calling PinConnectionListChanged on the input pin
								// This means PinConnectionListChanged might be called twice on the input, with the second time resulting in no actions
								InputEdNode->PinConnectionListChanged(InputPin);
							}
						}
					}
				}
			}
		}
	}

	Super::PinConnectionListChanged(Pin);
}

#endif // WITH_EDITOR && !UE_BUILD_SHIPPING

void UDataflowEdNode::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	Ar << DataflowNodeGuid;
#if WITH_EDITOR
	if (Ar.IsTransacting())
	{
		const UDataflow* const DataflowObject = Cast<UDataflow>(GetGraph());
		bool bCanSerializeNode = !DataflowObject || DataflowObject->IsPerNodeTransactionSerializationEnabled();
		if (bCanSerializeNode)
		{
			if (TSharedPtr<FDataflowNode> DataflowNode = GetDataflowNode())
			{
				DataflowNode->SerializeInternal(Ar);
			}
		}
	}
#endif
}

#if WITH_EDITOR

FSlateIcon UDataflowEdNode::GetIconAndTint(FLinearColor& OutColor) const
{
	FSlateIcon Icon;
	if (TSharedPtr<const FDataflowNode> DataflowNode = GetDataflowNode())
	{
		if (const FString* IconName = DataflowNode->TypedScriptStruct()->FindMetaData("Icon"))
		{
			Icon = FSlateIcon(FAppStyle::GetAppStyleSetName(), FName(*IconName));
		}
	}

	return Icon;
}

bool UDataflowEdNode::ShowPaletteIconOnNode() const
{
	return true;
}

FLinearColor UDataflowEdNode::GetNodeTitleColor() const
{
	if (DataflowGraph)
	{
		if (DataflowNodeGuid.IsValid())
		{
			if (TSharedPtr<FDataflowNode> DataflowNode = DataflowGraph->FindBaseNode(DataflowNodeGuid))
			{
				return UE::Dataflow::FNodeColorsRegistry::Get().GetNodeTitleColor(DataflowNode->GetCategory());
			}
		}
	}
	return FDataflowNode::DefaultNodeTitleColor;
}

FLinearColor UDataflowEdNode::GetNodeBodyTintColor() const
{
	if (const TSharedPtr<const FDataflowNode> DataflowNode = GetDataflowNode())
	{
		UE::Dataflow::FNodeColorsRegistry::Get().GetNodeBodyTintColor(DataflowNode->GetCategory());
	}
	return FDataflowNode::DefaultNodeBodyTintColor;
}

FText UDataflowEdNode::GetTooltipText() const
{
	if (const TSharedPtr<const FDataflowNode> DataflowNode = GetDataflowNode()) 
	{
		return FText::FromString(DataflowNode->GetToolTip());
	}

	return FText::FromString("");

}

FText UDataflowEdNode::GetPinDisplayName(const UEdGraphPin* Pin) const
{
	if (Pin)
	{
		if (const TSharedPtr<const FDataflowNode> DataflowNode = GetDataflowNode())
		{
			const FText DisplayName = DataflowNode->GetPinDisplayName(Pin->PinName, UE::Dataflow::Private::EdPinDirectionToDataflowDirection(Pin->Direction));
			if (!DisplayName.IsEmpty())
			{
				return DisplayName;
			}
		}
	}
	return Super::GetPinDisplayName(Pin);
}

void UDataflowEdNode::GetPinHoverText(const UEdGraphPin& Pin, FString& HoverTextOut) const
{
	if (const TSharedPtr<const FDataflowNode> DataflowNode = GetDataflowNode())
	{
		const UE::Dataflow::FPin::EDirection PinDirection = UE::Dataflow::Private::EdPinDirectionToDataflowDirection(Pin.Direction);

		FString MetaDataStr;
		TArray<FString> PinMetaData = DataflowNode->GetPinMetaData(Pin.PinName, PinDirection);
			
		if (Pin.Direction == EGPD_Input && PinMetaData.Contains(FDataflowNode::DataflowIntrinsic.ToString()))
		{
			MetaDataStr = "[Intrinsic]";
		}
		if (Pin.Direction == EGPD_Output && PinMetaData.Contains(FDataflowNode::DataflowPassthrough.ToString()))
		{
			MetaDataStr = "[Passthrough]";
		}

		FString NameStr = Pin.PinName.ToString();
		if (MetaDataStr.Len() > 0)
		{
			NameStr.Appendf(TEXT(" %s"), *MetaDataStr);
		}

		// find type information 
		FString TypeNameStr = Pin.PinType.PinCategory.ToString();

		const FDataflowConnection* Connection = nullptr;
		if (Pin.Direction == EGPD_Input)
		{
			Connection = DataflowNode->FindInput(Pin.PinName);
		}
		else if (Pin.Direction == EGPD_Output)
		{
			Connection = DataflowNode->FindOutput(Pin.PinName);
		}
		if (Connection)
		{
			TypeNameStr = Connection->GetPropertyTypeNameTooltip();
		}

		const FString PropertyTooltip = DataflowNode->GetPinToolTip(Pin.PinName, PinDirection);

		// put it all together 
		if (PropertyTooltip.IsEmpty())
		{
			HoverTextOut.Appendf(TEXT("%s\n%s"), *NameStr, *TypeNameStr);
		}
		else
		{
			HoverTextOut.Appendf(TEXT("%s\n%s\n\n%s"), *NameStr, *TypeNameStr, *PropertyTooltip);
		}
		
}
}

void UDataflowEdNode::AutowireNewNode(UEdGraphPin* FromPin)
{
	const UEdGraph* EdGraph = this->GetGraph();
	if (EdGraph == nullptr)
	{
		return;
	}

	if (!DataflowGraph || !FromPin)
	{
		return;
	}

	if (UEdGraphNode* FromGraphNode = FromPin->GetOwningNode())
	{
		if (FromPin->Direction == EEdGraphPinDirection::EGPD_Output)
		{
			for (UEdGraphPin* InputPin : this->GetAllPins())
			{
				if (InputPin->Direction == EEdGraphPinDirection::EGPD_Input)
				{
					if (this->PinIsCompatibleWithType(*InputPin, FromPin->PinType))
					{
						if (EdGraph->GetSchema()->TryCreateConnection(FromPin, InputPin))
						{
							FromGraphNode->NodeConnectionListChanged();
							this->NodeConnectionListChanged();
							return;
						}
					}
				}
			}
		}
		if (FromPin->Direction == EEdGraphPinDirection::EGPD_Input)
		{
			for (UEdGraphPin* OutputPin : this->GetAllPins())
			{
				if (OutputPin->Direction == EEdGraphPinDirection::EGPD_Output)
				{
					if (this->PinIsCompatibleWithType(*OutputPin, FromPin->PinType))
					{
						if (EdGraph->GetSchema()->TryCreateConnection(FromPin, OutputPin))
						{
							FromGraphNode->NodeConnectionListChanged();
							this->NodeConnectionListChanged();
							return;
						}
					}
				}
			}
		}
	}
}

void UDataflowEdNode::OnPinRemoved(UEdGraphPin* InRemovedPin)
{
	if (DataflowGraph && DataflowNodeGuid.IsValid())
	{
		if (const TSharedPtr<FDataflowNode> DataflowNode = DataflowGraph->FindBaseNode(DataflowNodeGuid))
		{
			if (InRemovedPin->Direction == EEdGraphPinDirection::EGPD_Input)
			{
				if (FDataflowInput* Con = DataflowNode->FindInput(FName(InRemovedPin->GetName())))
				{
					const UE::Dataflow::FPin Pin = { UE::Dataflow::FPin::EDirection::INPUT, Con->GetType(), Con->GetName() };
					DataflowNode->OnPinRemoved(Pin);
					DataflowNode->UnregisterPinConnection(Pin);
				}
			}
			else if (InRemovedPin->Direction == EEdGraphPinDirection::EGPD_Output)
			{
				if (FDataflowOutput* Con = DataflowNode->FindOutput(FName(InRemovedPin->GetName())))
				{
					const UE::Dataflow::FPin Pin = { UE::Dataflow::FPin::EDirection::OUTPUT, Con->GetType(), Con->GetName() };
					DataflowNode->OnPinRemoved(Pin);
					DataflowNode->UnregisterPinConnection(Pin);
				}
			}
		}
	}
}

bool UDataflowEdNode::ShouldDrawNodeAsControlPointOnly(int32& OutInputPinIndex, int32& OutOutputPinIndex) const
{
	UEdGraphNode::ShouldDrawNodeAsControlPointOnly(OutInputPinIndex, OutOutputPinIndex);
	if (const TSharedPtr<const FDataflowNode> DataflowNode = GetDataflowNode())
	{
		if (DataflowNode->GetType() == FDataflowReRouteNode::StaticType())
		{
			OutInputPinIndex = 0;
			OutOutputPinIndex = 1;
			return true;
		}
	}
	return false;
}

void UDataflowEdNode::PostEditUndo()
{
	Super::PostEditUndo();

	// Refresh the current graph, so the pins or whatever happened to this object can be reflected to the graph
	if (UEdGraph* const ParentGraph = GetGraph())
	{
		ParentGraph->NotifyGraphChanged();
	}

	// Make sure to re-sync the Dataflow connections
	for (UEdGraphPin* const Pin : GetAllPins())
	{
		PinConnectionListChanged(Pin);
	}
}

void UDataflowEdNode::HideAllInputPins()
{
	bool bAnyHidden = false;
	if (TSharedPtr<FDataflowNode> DataflowNode = GetDataflowNode())
	{
		TArray<FDataflowInput*> Inputs = DataflowNode->GetInputs();
		for (FDataflowInput* const Input : Inputs)
		{
			if (Input->GetCanHidePin() && !Input->GetPinIsHidden())
			{
				Input->SetPinIsHidden(true);
				if (!bAnyHidden)
				{
					Modify();
					bAnyHidden = true;
				}
				UEdGraphPin* const EdPin = FindPin(Input->GetName(), EEdGraphPinDirection::EGPD_Input);
				check(EdPin);
				EdPin->Modify();
				EdPin->bHidden = true;
			}
		}
	}

	if (bAnyHidden)
	{
		GetGraph()->NotifyGraphChanged();
	}
}

void UDataflowEdNode::ShowAllInputPins()
{
	bool bAnyUnhidden = false;
	if (TSharedPtr<FDataflowNode> DataflowNode = GetDataflowNode())
	{
		TArray<FDataflowInput*> Inputs = DataflowNode->GetInputs();
		for (FDataflowInput* const Input : Inputs)
		{
			if (Input->GetCanHidePin() && Input->GetPinIsHidden())
			{
				Input->SetPinIsHidden(false);
				if (!bAnyUnhidden)
				{
					Modify();
					bAnyUnhidden = true;
				}
				UEdGraphPin* const EdPin = FindPin(Input->GetName(), EEdGraphPinDirection::EGPD_Input);
				check(EdPin);
				EdPin->Modify();
				EdPin->bHidden = false;
			}
		}
	}

	if (bAnyUnhidden)
	{
		GetGraph()->NotifyGraphChanged();
	}
}

void UDataflowEdNode::ToggleHideInputPin(FName PinName)
{
	if (TSharedPtr<FDataflowNode> DataflowNode = GetDataflowNode())
	{
		if (FDataflowInput* const Input = DataflowNode->FindInput(PinName))
		{
			if (ensure(Input->GetCanHidePin()))
			{
				const bool bWasHidden = Input->GetPinIsHidden();
				Input->SetPinIsHidden(!bWasHidden);
				Modify();
				UEdGraphPin* const EdPin = FindPin(Input->GetName(), EEdGraphPinDirection::EGPD_Input);
				check(EdPin);
				EdPin->Modify();
				EdPin->bHidden = !bWasHidden;
				GetGraph()->NotifyGraphChanged();
			}
		}
	}
}

bool UDataflowEdNode::CanToggleHideInputPin(FName PinName) const
{
	if (TSharedPtr<const FDataflowNode> DataflowNode = GetDataflowNode())
	{
		if (const FDataflowInput* const Input = DataflowNode->FindInput(PinName))
		{
			if (Input->GetCanHidePin() && !Input->HasAnyConnections())
			{
				return true;
			}
		}
	}
	return false;
}

bool UDataflowEdNode::IsInputPinShown(FName PinName) const
{
	if (TSharedPtr<const FDataflowNode> DataflowNode = GetDataflowNode())
	{
		if (const FDataflowInput* const Input = DataflowNode->FindInput(PinName))
		{
			return !Input->GetPinIsHidden();
		}
	}
	return false;
}
#endif //WITH_EDITOR


TArray<UE::Dataflow::FRenderingParameter> UDataflowEdNode::GetRenderParameters() const
{
	if (TSharedPtr<const FDataflowNode> DataflowNode = GetDataflowNode())
	{
		return DataflowNode->GetRenderParameters();
	}
	return 	TArray<UE::Dataflow::FRenderingParameter>();
}


#undef LOCTEXT_NAMESPACE

