// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/Nodes/CustomizableObjectNodeModifierBase.h"

#include "MuCOE/EdGraphSchema_CustomizableObject.h"
#include "MuCOE/CustomizableObjectEditorLogger.h"
#include "MuCOE/Nodes/CustomizableObjectNodeObject.h"
#include "MuCOE/GraphTraversal.h"
#include "MuCO/CustomizableObjectCustomVersion.h"
#include "MuT/NodeModifier.h"


FLinearColor UCustomizableObjectNodeModifierBase::GetNodeTitleColor() const
{
	const UEdGraphSchema_CustomizableObject* Schema = GetDefault<UEdGraphSchema_CustomizableObject>();
	return Schema->GetPinTypeColor(Schema->PC_Modifier);
}


UEdGraphPin* UCustomizableObjectNodeModifierBase::OutputPin() const
{
	return FindPin(TEXT("Modifier"));
}


bool UCustomizableObjectNodeModifierBase::IsApplicableTo(UCustomizableObjectNode* Candidate)
{
	if (!Candidate)
	{
		return false;
	}

	const TArray<FString>* EnabledTags = Candidate->GetEnableTags();
	if (EnabledTags)
	{
		FString InternalTag = Candidate->GetInternalTag();

		switch ( MultipleTagPolicy )
		{
		case EMutableMultipleTagPolicy::OnlyOneRequired:
		{
			for (const FString& RequiredTag : RequiredTags)
			{
				if (InternalTag==RequiredTag || EnabledTags->Contains(RequiredTag))
				{
					return true;
				}
			}
			break;
		}

		case EMutableMultipleTagPolicy::AllRequired:
		{
			for (const FString& RequiredTag : RequiredTags)
			{
				if (InternalTag != RequiredTag && !EnabledTags->Contains(RequiredTag))
				{
					return false;
				}
			}
			return true;
			break;
		}

		default:
			// Not implemented
			check(false);
		}
	}

	return false;
}


void UCustomizableObjectNodeModifierBase::GetPossiblyModifiedNodes(TArray<UCustomizableObjectNode*>& CandidateNodes)
{
	// Scan all potential receivers
	UCustomizableObject* ThisNodeObject = GetRootObject(*this);
	UCustomizableObject* RootObject = GraphTraversal::GetRootObject(ThisNodeObject);

	TSet<UCustomizableObject*> AllCustomizableObject;
	GetAllObjectsInGraph(RootObject, AllCustomizableObject);

	for (const UCustomizableObject* CustObject : AllCustomizableObject)
	{
		if (CustObject)
		{
			for (const TObjectPtr<UEdGraphNode>& CandidateNode : CustObject->GetPrivate()->GetSource()->Nodes)
			{
				UCustomizableObjectNode* Typed = Cast<UCustomizableObjectNode>(CandidateNode);
				if (IsApplicableTo(Typed))
				{
					CandidateNodes.Add(Typed);
				}
			}
		}
	}
}

void UCustomizableObjectNodeModifierBase::BackwardsCompatibleFixup(int32 CustomizableObjectCustomVersion)
{
	Super::BackwardsCompatibleFixup(CustomizableObjectCustomVersion);
	
	const UEdGraphSchema_CustomizableObject* Schema = GetDefault<UEdGraphSchema_CustomizableObject>();

	// Remove "Material" pin and add "Modifier", fix the other side of the connections, if possible
	if (CustomizableObjectCustomVersion == FCustomizableObjectCustomVersion::AddModifierPin)
	{
		// Replace the output pin
		UEdGraphPin* OldPin = FindPin(TEXT("Material"));
		if (OldPin)
		{
			UEdGraphPin* NewPin = FindPin(TEXT("Modifier")); 
			if (!NewPin)
			{
				NewPin = CustomCreatePin(EGPD_Output, Schema->PC_Modifier, FName("Modifier"));
			}

			FGuid PinId = NewPin->PinId;
			NewPin->CopyPersistentDataFromOldPin(*OldPin);
			NewPin->PinId = PinId;
			NewPin->bHidden = OldPin->bHidden;

			CustomRemovePin(*OldPin);

			// Reconnect it to the correct output of the target node
			TArray<UEdGraphPin*> LinksToRemove;
			for (int32 LinkIndex = 0; LinkIndex < NewPin->LinkedTo.Num(); ++LinkIndex)
			{
				UEdGraphPin* LinkedToPin = NewPin->LinkedTo[LinkIndex];

				if (!LinkedToPin)
				{
					continue;
				}

				UEdGraphNode* ToNode = LinkedToPin->GetOwningNode();
				if (!ToNode)
				{
					continue;
				}

				if (UCustomizableObjectNodeObject* ToObjectNode = Cast<UCustomizableObjectNodeObject>(ToNode))
				{
					UEdGraphPin* ToModifierPin = ToObjectNode->ModifiersPin();
					if (ToModifierPin == LinkedToPin)
					{
						// It is already correctly connected
						continue;
					}

					if (ToModifierPin)
					{
						LinksToRemove.Add(LinkedToPin);
						NewPin->MakeLinkTo(ToModifierPin);
					}
				}
			}

			// Remove reconnected links
			for (UEdGraphPin* LinkToRemove : LinksToRemove)
			{
				NewPin->BreakLinkTo(LinkToRemove);
			}
		}
	}

	// Fix the "Modifier" pin connection
	if (CustomizableObjectCustomVersion == FCustomizableObjectCustomVersion::FixModifierPin)
	{
		// Replace the output pin
		UEdGraphPin* ModifierPin = FindPin(TEXT("Modifier"));
		if (ModifierPin)
		{
			// Reconnect it to the correct output of the target node
			TArray<UEdGraphPin*> LinksToRemove;
			for (int32 LinkIndex = 0; LinkIndex < ModifierPin->LinkedTo.Num(); ++LinkIndex)
			{
				UEdGraphPin* LinkedToPin = ModifierPin->LinkedTo[LinkIndex];

				if (!LinkedToPin)
				{
					continue;
				}

				UEdGraphNode* ToNode = LinkedToPin->GetOwningNode();
				if (!ToNode)
				{
					continue;
				}

				if (UCustomizableObjectNodeObject* ToObjectNode = Cast<UCustomizableObjectNodeObject>(ToNode))
				{
					UEdGraphPin* ToModifierPin = ToObjectNode->ModifiersPin();
					if (ToModifierPin == LinkedToPin)
					{
						// It is already correctly connected
						continue;
					}

					if (ToModifierPin)
					{
						LinksToRemove.Add(LinkedToPin);
						ModifierPin->MakeLinkTo(ToModifierPin);
					}
					else
					{
						ensure(false);
					}
				}
				else
				{
					// The modifier is connected to a node for which automatic upgrade support is not implemented. This will be warned in PostBackwardsCompatibleFixup
				}
			}

			// Remove reconnected links
			for (UEdGraphPin* LinkToRemove : LinksToRemove)
			{
				ModifierPin->BreakLinkTo(LinkToRemove);
			}
		}
	}
}

void UCustomizableObjectNodeModifierBase::PostBackwardsCompatibleFixup()
{
	Super::PostBackwardsCompatibleFixup();

	const UEdGraphSchema_CustomizableObject* Schema = GetDefault<UEdGraphSchema_CustomizableObject>();

	// Check for old legacy connections that need manual update
	{
		UEdGraphPin* NewPin = FindPin(TEXT("Modifier"));
		if (NewPin)
		{
			for (int32 LinkIndex = 0; LinkIndex < NewPin->LinkedTo.Num(); ++LinkIndex)
			{
				UEdGraphPin* LinkedToPin = NewPin->LinkedTo[LinkIndex];

				if (!LinkedToPin)
				{
					continue;
				}

				UEdGraphNode* ToNode = LinkedToPin->GetOwningNode();
				if (!ToNode)
				{
					continue;
				}

				if (LinkedToPin->PinType.PinCategory != Schema->PC_Modifier)
				{
					// The modifier is connected to a node for which automatic upgrade support is not implemented
					FString Msg = FString::Printf(TEXT("A modifier node has a legacy connection to a node [%s] without automatic upgrade support. Manual update is probably needed."), *ToNode->GetName());
					FCustomizableObjectEditorLogger::CreateLog(FText::FromString(Msg))
						.Severity(EMessageSeverity::Warning)
						.Context(*this)
						.BaseObject(true)
						.Log();

				}
			}
		}
	}

	// Apply backwards compatibility auto-generated tags to external objects.
	for (const FLegacyTag& Tag : LegacyBackportsRequiredTags)
	{
		// If we still have it in this node
		if (RequiredTags.Contains(Tag.Tag))
		{
			UCustomizableObjectNode* ParentNode = GetCustomizableObjectExternalNode<UCustomizableObjectNode>(Tag.ParentObject.Get(), Tag.ParentNode);
			if (ParentNode)
			{
				TArray<FString>* NodeEnableTags = ParentNode->GetEnableTags();
				if (NodeEnableTags)
				{
					NodeEnableTags->AddUnique(Tag.Tag);
				}
			}
		}
	}
}


FString UCustomizableObjectNodeModifierBase::MakeNodeAutoTag( UEdGraphNode* Node )
{
	const FString PackageName = Node->GetOutermost()->GetPathName();
	const FString ShortName = FPackageName::GetShortName(PackageName);
	FString NewLegacyTag = FString::Printf(TEXT("%s_%s"), *ShortName, *Node->NodeGuid.ToString());
	return NewLegacyTag;
}
