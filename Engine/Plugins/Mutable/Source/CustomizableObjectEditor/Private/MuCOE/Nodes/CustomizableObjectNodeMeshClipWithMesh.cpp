// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/Nodes/CustomizableObjectNodeModifierClipWithMesh.h"

#include "MuCOE/CustomizableObjectGraph.h"
#include "MuCOE/EdGraphSchema_CustomizableObject.h"
#include "MuCOE/ICustomizableObjectEditor.h"
#include "MuCO/CustomizableObjectCustomVersion.h"
#include "MuCOE/GraphTraversal.h"

class UCustomizableObjectNodeRemapPins;

#define LOCTEXT_NAMESPACE "CustomizableObjectEditor"


UCustomizableObjectNodeModifierClipWithMesh::UCustomizableObjectNodeModifierClipWithMesh()
{
	Transform = FTransform::Identity;
}


void UCustomizableObjectNodeModifierClipWithMesh::AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins)
{
	const UEdGraphSchema_CustomizableObject* Schema = GetDefault<UEdGraphSchema_CustomizableObject>();

	UEdGraphPin* ClipMeshPin = CustomCreatePin(EGPD_Input, Schema->PC_Mesh, FName("Clip Mesh"));
	ClipMeshPin->bDefaultValueIsIgnored = true;

	UEdGraphPin* OutputPin_p = CustomCreatePin(EGPD_Output, Schema->PC_Modifier, FName("Modifier"));
}


void UCustomizableObjectNodeModifierClipWithMesh::BackwardsCompatibleFixup(int32 CustomizableObjectCustomVersion)
{
	Super::BackwardsCompatibleFixup(CustomizableObjectCustomVersion);

	const UEdGraphSchema_CustomizableObject* Schema = GetDefault<UEdGraphSchema_CustomizableObject>();

	if (CustomizableObjectCustomVersion == FCustomizableObjectCustomVersion::UnifyRequiredTags)
	{
		RequiredTags = Tags_DEPRECATED;
		Tags_DEPRECATED.Empty();
	}
	
	if (CustomizableObjectCustomVersion == FCustomizableObjectCustomVersion::ModifierClipWithMeshCleanup)
	{
		for ( const FGuid& MeshSectionNodeID : ArrayMaterialNodeToClipWithID_DEPRECATED )
		{
			// Look for the parent material and set it as the modifier reference material 
			UCustomizableObjectNode* ParentNode = GetCustomizableObjectExternalNode<UCustomizableObjectNode>(CustomizableObjectToClipWith_DEPRECATED.Get(), MeshSectionNodeID);

			if (ParentNode)
			{
				// Add an autogenerated tag to the legacy parent, so that it can be referenced in this modifier.
				FString NewLegacyTag = MakeNodeAutoTag(ParentNode);
				RequiredTags.Add(NewLegacyTag);

				TArray<FString>* NodeEnableTags = ParentNode->GetEnableTags();
				if (NodeEnableTags)
				{
					NodeEnableTags->AddUnique(NewLegacyTag);
				}
				else
				{
					// Conversion failed?
					ensure(false);
					UE_LOG(LogMutable, Warning, TEXT("[%s] UCustomizableObjectNodeModifierClipWithMesh version upgrade failed."), *GetOutermost()->GetName());
				}

				// If the tag was added to another CO, keep track of the addition to be able to repeat it in case
				// that CO is not re-saved but this one is.
				UCustomizableObject* ThisNodeObject = GetRootObject(*this);
				if (CustomizableObjectToClipWith_DEPRECATED != ThisNodeObject)
				{
					FLegacyTag LegacyTag;
					LegacyTag.ParentObject = CustomizableObjectToClipWith_DEPRECATED;
					LegacyTag.ParentNode = MeshSectionNodeID;
					LegacyTag.Tag = NewLegacyTag;
					LegacyBackportsRequiredTags.AddUnique(LegacyTag);
				}
			}
			else
			{
				UE_LOG(LogMutable, Log, TEXT("[%s] UCustomizableObjectNodeModifierClipWithMesh has no parent. It will not be upgraded."), *GetOutermost()->GetName());
			}
		}

		ArrayMaterialNodeToClipWithID_DEPRECATED.Empty();
	}
}


FText UCustomizableObjectNodeModifierClipWithMesh::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("Clip_Mesh_With_Mesh", "Clip Mesh With Mesh");
}


UEdGraphPin* UCustomizableObjectNodeModifierClipWithMesh::OutputPin() const
{
	return FindPin(TEXT("Modifier"));
}


UEdGraphPin* UCustomizableObjectNodeModifierClipWithMesh::ClipMeshPin() const
{
	return FindPin(TEXT("Clip Mesh"));
}


FText UCustomizableObjectNodeModifierClipWithMesh::GetTooltipText() const
{
	return LOCTEXT("Clip_Mesh_Mesh_Tooltip", "Removes the part of a mesh section that is completely enclosed in a mesh volume.\nIt only removes the faces that fall completely inside the cutting volume, along with the vertices and edges that define only faces that are deleted.");
}

#undef LOCTEXT_NAMESPACE