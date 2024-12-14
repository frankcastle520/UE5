// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/Nodes/CustomizableObjectNodeExternalPin.h"

#include "MuCO/CustomizableObjectCustomVersion.h"
#include "MuCOE/CustomizableObjectGraph.h"
#include "MuCOE/CustomizableObjectPin.h"
#include "MuCOE/EdGraphSchema_CustomizableObject.h"
#include "MuCOE/GraphTraversal.h"
#include "MuCOE/Nodes/CustomizableObjectNodeExposePin.h"
#include "MuCOE/RemapPins/CustomizableObjectNodeRemapPinsByPosition.h"

#define LOCTEXT_NAMESPACE "CustomizableObjectEditor"


void UCustomizableObjectNodeExternalPin::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	Ar.UsingCustomVersion(FCustomizableObjectCustomVersion::GUID);
}


void UCustomizableObjectNodeExternalPin::BackwardsCompatibleFixup(int32 CustomizableObjectCustomVersion)
{
	Super::BackwardsCompatibleFixup(CustomizableObjectCustomVersion);
	
	// Get the pin type from the actual pin.
	if (GetLinkerCustomVersion(FCustomizableObjectCustomVersion::GUID) < FCustomizableObjectCustomVersion::BeforeCustomVersionWasAdded)
	{
		if (PinType.IsNone())
		{
			PinType = Pins[0]->PinType.PinCategory;
		}
	}
}


void UCustomizableObjectNodeExternalPin::PostBackwardsCompatibleFixup()
{
	Super::PostBackwardsCompatibleFixup();

	if (UCustomizableObjectNodeExposePin* NodeExposePin = GetNodeExposePin())
	{
		OnNameChangedDelegateHandle = NodeExposePin->OnNameChangedDelegate.AddUObject(this, &Super::ReconstructNode);
		DestroyNodeDelegateHandle = NodeExposePin->DestroyNodeDelegate.AddUObject(this, &Super::ReconstructNode);
	}

	// Reconstruct the node since the NodeExposePin pin name may have changed while not loaded.
	Super::ReconstructNode();
}


void UCustomizableObjectNodeExternalPin::SetExternalObjectNodeId(FGuid Guid)
{
	PrePropagateConnectionChanged();

	if (UCustomizableObjectNodeExposePin* NodeExposePin = GetNodeExposePin())
	{
		NodeExposePin->OnNameChangedDelegate.Remove(OnNameChangedDelegateHandle);
		NodeExposePin->DestroyNodeDelegate.Remove(DestroyNodeDelegateHandle);
	}

	ExternalObjectNodeId = Guid;

	if (UCustomizableObjectNodeExposePin* NodeExposePin = GetNodeExposePin())
	{
		OnNameChangedDelegateHandle = NodeExposePin->OnNameChangedDelegate.AddUObject(this, &Super::ReconstructNode);
		DestroyNodeDelegateHandle = NodeExposePin->DestroyNodeDelegate.AddUObject(this, &Super::ReconstructNode);
	}
	
	Super::ReconstructNode();

	PropagateConnectionChanged();
}


UEdGraphPin* UCustomizableObjectNodeExternalPin::GetExternalPin() const
{
	return Pins[0];
}


UCustomizableObjectNodeExposePin* UCustomizableObjectNodeExternalPin::GetNodeExposePin() const
{
	return GetCustomizableObjectExternalNode<UCustomizableObjectNodeExposePin>(ExternalObject, ExternalObjectNodeId);
}


void UCustomizableObjectNodeExternalPin::PrePropagateConnectionChanged()
{
	PropagatePreviousPin = ReverseFollowPinArray(*Pins[0]);
}


void UCustomizableObjectNodeExternalPin::PropagateConnectionChanged()
{
	// Propagate new left.
	PropagatePreviousPin.Append(ReverseFollowPinArray(*Pins[0])); // Notify old connections and new connections.
	NodePinConnectionListChanged(PropagatePreviousPin); // This function avoids double notifications.
	
	// Propagate right.
	NodePinConnectionListChanged(FollowOutputPinArray(*Pins[0]));
}


void UCustomizableObjectNodeExternalPin::AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins)
{
	FName PinName = GetNodeExposePin() ? FName(GetNodeExposePin()->GetNodeName()) : FName("Object");
	const bool bIsArrayPinCategory = PinType == UEdGraphSchema_CustomizableObject::PC_GroupProjector;

	CustomCreatePin(EGPD_Output, PinType, PinName, bIsArrayPinCategory);
}


FText UCustomizableObjectNodeExternalPin::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	const FText PinTypeName = UEdGraphSchema_CustomizableObject::GetPinCategoryFriendlyName(PinType);

	if (TitleType == ENodeTitleType::ListView || !ExternalObject)
	{
		return FText::Format(LOCTEXT("External_Pin_Title", "Import {0} Pin"), PinTypeName);
	}
	else
	{
		return FText::Format(LOCTEXT("External_Pin_Title_WithName", "{0}\nImport {1} Pin"), FText::FromString(ExternalObject->GetName()), PinTypeName);
	}
}


FLinearColor UCustomizableObjectNodeExternalPin::GetNodeTitleColor() const
{
	const UEdGraphSchema_CustomizableObject* Schema = GetDefault<UEdGraphSchema_CustomizableObject>();
	return Schema->GetPinTypeColor(PinType);
}


FText UCustomizableObjectNodeExternalPin::GetTooltipText() const
{
	return LOCTEXT("Import_Pin_Tooltip", "Make use of a value defined elsewhere in this Customizable Object hierarchy.");
}

bool UCustomizableObjectNodeExternalPin::CanConnect(const UEdGraphPin* InOwnedInputPin, const UEdGraphPin* InOutputPin, bool& bOutIsOtherNodeBlocklisted, bool& bOutArePinsCompatible) const
{
	// Chech the pin types do match
	bOutArePinsCompatible = Super::CanConnect(InOwnedInputPin, InOutputPin, bOutIsOtherNodeBlocklisted, bOutArePinsCompatible);

	// Check the type of the other node to make sure it is not one we do not want to allow the connection with
	bOutIsOtherNodeBlocklisted = Cast<UCustomizableObjectNodeExposePin>(InOutputPin->GetOwningNode()) != nullptr;

	return bOutArePinsCompatible && !bOutIsOtherNodeBlocklisted;
}


void UCustomizableObjectNodeExternalPin::ReconstructNode(UCustomizableObjectNodeRemapPins* RemapPinsMode)
{
	Super::ReconstructNode(RemapPinsMode);

	if (!ExternalObject)
	{
		ExternalObject = Cast<UCustomizableObject>(GetOutermostObject());
		ExternalObjectNodeId = FGuid();
	}
}


void UCustomizableObjectNodeExternalPin::BeginPostDuplicate(bool bDuplicateForPIE)
{
	Super::BeginPostDuplicate(bDuplicateForPIE);

	if (ExternalObjectNodeId.IsValid())
	{
		if (UCustomizableObjectGraph* CEdGraph = Cast<UCustomizableObjectGraph>(GetGraph()))
		{
			ExternalObjectNodeId = CEdGraph->RequestNotificationForNodeIdChange(ExternalObjectNodeId, NodeGuid);
		}
	}
}


void UCustomizableObjectNodeExternalPin::UpdateReferencedNodeId(const FGuid& NewGuid)
{
	ExternalObjectNodeId = NewGuid;
}


void UCustomizableObjectNodeExternalPin::PreEditChange(FProperty* PropertyAboutToChange)
{
	Super::PreEditChange(PropertyAboutToChange);
	
	if (PropertyAboutToChange)
	{
		if (PropertyAboutToChange->GetFName() == GET_MEMBER_NAME_CHECKED(UCustomizableObjectNodeExternalPin, ExternalObject))
		{
			PrePropagateConnectionChanged();
		}
	}
}


void UCustomizableObjectNodeExternalPin::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (const FProperty* PropertyThatChanged = PropertyChangedEvent.Property)
	{
		if (PropertyThatChanged->GetFName() == GET_MEMBER_NAME_CHECKED(UCustomizableObjectNodeExternalPin, ExternalObject))
		{
			PropagateConnectionChanged();
		}
	}
}


UCustomizableObjectNodeRemapPins* UCustomizableObjectNodeExternalPin::CreateRemapPinsDefault() const
{
	return NewObject<UCustomizableObjectNodeRemapPinsByPosition>();
}


#undef LOCTEXT_NAMESPACE
