// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/Nodes/CustomizableObjectNodeModifierEditMeshSection.h"

#include "MuCOE/Nodes/CustomizableObjectNodeModifierExtendMeshSection.h"
#include "MuCOE/CustomizableObjectEditor_Deprecated.h"
#include "MuCOE/CustomizableObjectLayout.h"
#include "MuCOE/EdGraphSchema_CustomizableObject.h"
#include "MuCOE/CustomizableObjectPin.h"
#include "MuCO/CustomizableObjectCustomVersion.h"
#include "MuCOE/Nodes/CustomizableObjectNodeSkeletalMesh.h"
#include "MuCOE/Nodes/CustomizableObjectNodeTable.h"
#include "MuCOE/Nodes/CustomizableObjectNodeLayoutBlocks.h"
#include "MuCOE/GraphTraversal.h"
#include "MaterialTypes.h"

class FCustomizableObjectNodeParentedMaterial;
class UCustomizableObjectNodeRemapPins;

#define LOCTEXT_NAMESPACE "CustomizableObjectEditor"


void UCustomizableObjectNodeModifierEditMeshSection::AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins)
{
	const UEdGraphSchema_CustomizableObject* Schema = GetDefault<UEdGraphSchema_CustomizableObject>();
	
	const int32 NumImages = GetNumParameters(EMaterialParameterType::Texture);
	for (int32 ImageIndex = 0; ImageIndex < NumImages; ++ImageIndex)
	{
		{
			UCustomizableObjectNodeEditMaterialPinEditImageData* PinEditImageData = NewObject<UCustomizableObjectNodeEditMaterialPinEditImageData>(this);
			PinEditImageData->ImageParamId = GetParameterId(EMaterialParameterType::Texture, ImageIndex);
			
			const FName ImageName = GetParameterName(EMaterialParameterType::Texture, ImageIndex);
			UEdGraphPin* PinImage = CustomCreatePin(EGPD_Input, Schema->PC_Image, ImageName, PinEditImageData);
			PinImage->bHidden = true;
			PinImage->bDefaultValueIsIgnored = true;

			PinsParameterMap.Add(PinEditImageData->ImageParamId, FEdGraphPinReference(PinImage));

			FString PinMaskName = ImageName.ToString() + FString(TEXT(" Mask"));
			UEdGraphPin* PinMask = CustomCreatePin(EGPD_Input, Schema->PC_Image, *PinMaskName);
			PinMask->bHidden = true;
			PinMask->bDefaultValueIsIgnored = true;
			
			PinEditImageData->PinMask = FEdGraphPinReference(PinMask);
		}
	}

	CustomCreatePin(EGPD_Output, Schema->PC_Modifier, TEXT("Modifier"));
}


const UEdGraphPin* UCustomizableObjectNodeModifierEditMeshSection::GetUsedImageMaskPin(const FNodeMaterialParameterId& ImageId) const
{
	if (const UEdGraphPin* Pin = GetUsedImagePin(ImageId))
	{
		UCustomizableObjectNodeEditMaterialPinEditImageData& PinData = GetPinData<UCustomizableObjectNodeEditMaterialPinEditImageData>(*Pin);
		return PinData.PinMask.Get();
	}

	return nullptr;
}


bool UCustomizableObjectNodeModifierEditMeshSection::IsSingleOutputNode() const
{
	return true;
}


bool UCustomizableObjectNodeModifierEditMeshSection::CustomRemovePin(UEdGraphPin& Pin)
{
	for (TMap<FNodeMaterialParameterId, FEdGraphPinReference>::TIterator It = PinsParameterMap.CreateIterator(); It; ++It)
	{
		if (It.Value().Get() == &Pin) // We could improve performance if FEdGraphPinReference exposed the pin id.
		{
			It.RemoveCurrent();
			break;
		}
	}

	return Super::CustomRemovePin(Pin);
}


bool UCustomizableObjectNodeModifierEditMeshSection::HasPinViewer() const
{
	return true;
}


void UCustomizableObjectNodeModifierEditMeshSection::BackwardsCompatibleFixup(int32 CustomizableObjectCustomVersion)
{
	Super::BackwardsCompatibleFixup(CustomizableObjectCustomVersion);

	const auto GetLayouts = [](UCustomizableObjectNodeMaterialBase& ParentMaterialNode, TArray<UCustomizableObjectLayout*>& OutLayouts)
		{
			UCustomizableObjectNodeMaterial* NodeMaterial = ParentMaterialNode.GetMaterialNode();
			if (!NodeMaterial)
			{
				return;
			}

			UEdGraphPin* MeshPin = NodeMaterial->GetMeshPin();
			if (!MeshPin)
			{
				return;
			}

			const UEdGraphPin* ConnectedPin = FollowInputPin(*MeshPin);
			if (!ConnectedPin)
			{
				return;
			}

			const UEdGraphPin* SourceMeshPin = FindMeshBaseSource(*ConnectedPin, false);
			if (!SourceMeshPin)
			{
				return;
			}

			if (const UCustomizableObjectNodeSkeletalMesh* MeshNode = Cast<UCustomizableObjectNodeSkeletalMesh>(SourceMeshPin->GetOwningNode()))
			{
				UCustomizableObjectNodeSkeletalMeshPinDataMesh* MeshPinData = Cast<UCustomizableObjectNodeSkeletalMeshPinDataMesh>(MeshNode->GetPinData(*SourceMeshPin));
				
				if (!MeshPinData)
				{
					return;
				}

				// The custom version of SkeletalMesh nodes may be up to date if they're in a different CO. 
				OutLayouts = MeshPinData->Layouts;

				if (OutLayouts.IsEmpty())
				{
					// Pre FCustomizableObjectCustomVersion::RemoveNodeLayout code. Get Layouts from NodeLayoutBlocks
					TArray<UEdGraphPin*> NonOrphanPins = MeshNode->GetAllNonOrphanPins();
					for (const UEdGraphPin* Pin : NonOrphanPins)
					{
						if (const UCustomizableObjectNodeSkeletalMeshPinDataLayout* PinData = Cast<UCustomizableObjectNodeSkeletalMeshPinDataLayout>(MeshNode->GetPinData(*Pin)))
						{
							if (PinData->GetLODIndex() == MeshPinData->GetLODIndex() &&
								PinData->GetSectionIndex() == MeshPinData->GetSectionIndex())
							{
								if (const UEdGraphPin* SourceLayoutConnectedPin = FollowInputPin(*Pin))
								{
									const UCustomizableObjectNodeLayoutBlocks* LayoutNode = Cast<UCustomizableObjectNodeLayoutBlocks>(SourceLayoutConnectedPin->GetOwningNode());
									if (LayoutNode && LayoutNode->Layout)
									{
										OutLayouts.Add(LayoutNode->Layout);
									}
								}
							}
						}
					}
				}
			}
			else if (const UCustomizableObjectNodeTable* TableNode = Cast<UCustomizableObjectNodeTable>(SourceMeshPin->GetOwningNode()))
			{
				OutLayouts = TableNode->GetLayouts(SourceMeshPin);
			}
		};

	// Convert deprecated node index list to the node id list.
	if (CustomizableObjectCustomVersion == FCustomizableObjectCustomVersion::PostLoadToCustomVersion
		&& BlockIds_DEPRECATED.Num() < Blocks_DEPRECATED.Num())
	{
		UCustomizableObjectNodeMaterialBase* ParentMaterialNode = GetCustomizableObjectExternalNode<UCustomizableObjectNodeMaterialBase>(ParentMaterialObject_DEPRECATED.Get(), ParentMaterialNodeId_DEPRECATED);

		TArray<UCustomizableObjectLayout*> Layouts;
		GetLayouts(*ParentMaterialNode, Layouts);

		if (!Layouts.IsValidIndex(ParentLayoutIndex))
		{
			UE_LOG(LogMutable, Warning, TEXT("[%s] UCustomizableObjectNodeModifierEditMeshSection refers to an invalid texture layout index %d. Parent node has %d layouts."), 
				*GetOutermost()->GetName(), ParentLayoutIndex, Layouts.Num());
		}
		else
		{
			UCustomizableObjectLayout* ParentLayout = Layouts[ParentLayoutIndex];

			if (Cast<UCustomizableObjectNodeMaterial>(ParentMaterialNode))
			{
				for (int32 IndexIndex = BlockIds_DEPRECATED.Num(); IndexIndex < Blocks_DEPRECATED.Num(); ++IndexIndex)
				{
					int32 BlockIndex = Blocks_DEPRECATED[IndexIndex];
					if (ParentLayout->Blocks.IsValidIndex(BlockIndex) )
					{
						const FGuid Id = ParentLayout->Blocks[BlockIndex].Id;
						if (Id.IsValid())
						{
							BlockIds_DEPRECATED.Add(Id);
						}
						else
						{
							UE_LOG(LogMutable, Warning, TEXT("[%s] UCustomizableObjectNodeModifierEditMeshSection refers to an valid layout block %d but that block doesn't have an id."),
								*GetOutermost()->GetName(), BlockIndex );
						}
					}
					else
					{
						UE_LOG(LogMutable, Warning, TEXT("[%s] UCustomizableObjectNodeModifierEditMeshSection refers to an invalid layout block index %d. Parent node has %d blocks."),
							*GetOutermost()->GetName(), BlockIndex, ParentLayout->Blocks.Num());
					}
				}
			}
		}
	}
	
	// Convert deprecated node id list to absolute rect list.
	if (CustomizableObjectCustomVersion == FCustomizableObjectCustomVersion::UseUVRects)
	{
		// If we are here, it means this node was loaded from a version that didn't have it's own layout.
		check(Layout->Blocks.IsEmpty());

		UCustomizableObjectNodeMaterialBase* ParentMaterialNode = GetCustomizableObjectExternalNode<UCustomizableObjectNodeMaterialBase>(ParentMaterialObject_DEPRECATED.Get(), ParentMaterialNodeId_DEPRECATED);

		TArray<UCustomizableObjectLayout*> ParentLayouts;
		GetLayouts(*ParentMaterialNode, ParentLayouts);

		if (!ParentLayouts.IsValidIndex(ParentLayoutIndex))
		{
			UE_LOG(LogMutable, Warning, TEXT("[%s] UCustomizableObjectNodeModifierEditMeshSection refers to an invalid texture layout index %d. Parent node has %d layouts."),
				*GetOutermost()->GetName(), ParentLayoutIndex, ParentLayouts.Num());
		}
		else
		{
			UCustomizableObjectLayout* ParentLayout = ParentLayouts[ParentLayoutIndex];
			FIntPoint GridSize = ParentLayout->GetGridSize();

			Layout->SetGridSize(GridSize);

			if (Cast<UCustomizableObjectNodeMaterial>(ParentMaterialNode))
			{
				for (const FGuid& BlockId : BlockIds_DEPRECATED)
				{
					bool bSkipBlock = false;
					for (const FCustomizableObjectLayoutBlock& ExistingBlock : Layout->Blocks)
					{
						if (ExistingBlock.Id == BlockId)
						{
							bSkipBlock = true;
							UE_LOG(LogMutable, Log, TEXT("[%s] UCustomizableObjectNodeModifierEditMeshSection has a duplicated layout block id. One has been ignored during version upgrade."), *GetOutermost()->GetName());
							break;
						}
					}

					if (bSkipBlock)
					{
						continue;
					}

					bool bFoundInParent = false;
					for (const FCustomizableObjectLayoutBlock& ParentBlock : ParentLayout->Blocks)
					{
						if (ParentBlock.Id == BlockId)
						{
							bFoundInParent = true;

							FCustomizableObjectLayoutBlock NewBlock;
							NewBlock = ParentBlock;

							// Clear some unnecessary data.
							NewBlock.bReduceBothAxes = false;
							NewBlock.bReduceByTwo = false;
							NewBlock.Priority = 0;

							Layout->Blocks.Add(NewBlock);
							break;
						}
					}

					if (!bFoundInParent)
					{
						UE_LOG(LogMutable, Warning, TEXT("[%s] UCustomizableObjectNodeModifierEditMeshSection refers to and invalid layout block. It has been ignored during version upgrade."), *GetOutermost()->GetName());
					}
				}
			}
		}
	}

	if (CustomizableObjectCustomVersion == FCustomizableObjectCustomVersion::AutomaticNodeMaterial)
	{
		UCustomizableObjectNodeMaterialBase* ParentMaterial = GetCustomizableObjectExternalNode<UCustomizableObjectNodeMaterialBase>(ParentMaterialObject_DEPRECATED.Get(), ParentMaterialNodeId_DEPRECATED);

		if (ParentMaterial)
		{
			for (const FCustomizableObjectNodeEditMaterialImage& Image : Images_DEPRECATED)
			{
				const UEdGraphPin* ImagePin = FindPin(Image.Name);
				const UEdGraphPin* PinMask = FindPin(Image.Name + " Mask");
				if (!ImagePin || !PinMask)
				{
					continue;
				}
				
				UCustomizableObjectNodeEditMaterialPinEditImageData*  PinEditImageData = NewObject<UCustomizableObjectNodeEditMaterialPinEditImageData>(this);
				PinEditImageData->ImageId_DEPRECATED = FGuid::NewGuid();
				PinEditImageData->PinMask = PinMask;
				
				// Search for the Image Id the Edit pin was referring to.
				const int32 NumImages = ParentMaterial->GetNumParameters(EMaterialParameterType::Texture);
				for (int32 ImageIndex = 0; ImageIndex < NumImages; ++ImageIndex)
				{
					if (ParentMaterial->GetParameterName(EMaterialParameterType::Texture, ImageIndex).ToString() == Image.Name)
					{
						PinEditImageData->ImageId_DEPRECATED = ParentMaterial->GetParameterId(EMaterialParameterType::Texture, ImageIndex).ParameterId;
						break;
					}
				}
				
				AddPinData(*ImagePin, *PinEditImageData);
			}
		}
		
		Images_DEPRECATED.Empty();
	}

	// Fill PinsParameter.
	if (CustomizableObjectCustomVersion == FCustomizableObjectCustomVersion::AutomaticNodeMaterialPerformanceBug) // || CustomizableObjectCustomVersion < FCustomizableObjectCustomVersion::AutomaticNodeMaterialPerformance
	{
		for (const UEdGraphPin* Pin : GetAllNonOrphanPins())
		{
			if (const UCustomizableObjectNodeEditMaterialPinEditImageData* PinData = Cast<UCustomizableObjectNodeEditMaterialPinEditImageData>(GetPinData(*Pin)))
			{
				PinsParameter_DEPRECATED.Add(PinData->ImageId_DEPRECATED, FEdGraphPinReference(Pin));
			}
		}
	}

	if (CustomizableObjectCustomVersion == FCustomizableObjectCustomVersion::ConvertEditAndExtendToModifiers)
	{
		// Look for the parent material and set it as the modifier reference material 

		PinsParameterMap = PinsParameterMap_DEPRECATED;
		PinsParameterMap_DEPRECATED.Empty();

		UCustomizableObjectNode* ParentNode = GetCustomizableObjectExternalNode<UCustomizableObjectNode>(ParentMaterialObject_DEPRECATED.Get(), ParentMaterialNodeId_DEPRECATED);

		if (UCustomizableObjectNodeMaterial* MaterialParentNode = Cast<UCustomizableObjectNodeMaterial>(ParentNode))
		{
			ReferenceMaterial = MaterialParentNode->GetMaterial();
		}
		else if (UCustomizableObjectNodeModifierExtendMeshSection* ExtendParentNode = Cast<UCustomizableObjectNodeModifierExtendMeshSection>(ParentNode))
		{
			ReferenceMaterial = ExtendParentNode->ReferenceMaterial;
		}
		else
		{
			// Conversion failed?
			ensure(false);
			UE_LOG(LogMutable, Warning, TEXT("[%s] UCustomizableObjectNodeModifierExtendMeshSection version upgrade failed."), *GetOutermost()->GetName());
		}

		ReconstructNode();
	}
}


FText UCustomizableObjectNodeModifierEditMeshSection::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("Edit_MeshSection", "Edit Mesh Section");
}


FString UCustomizableObjectNodeModifierEditMeshSection::GetRefreshMessage() const
{
	return TEXT("Source material has changed, texture channels might have been added, removed or renamed. Please refresh the parent material node to reflect those changes.");
}


FText UCustomizableObjectNodeModifierEditMeshSection::GetTooltipText() const
{
	return LOCTEXT("Edit_Material_Tooltip", "Modify the texture parameters of an ancestor's material partially or completely.");
}


void UCustomizableObjectNodeModifierEditMeshSection::SetLayoutIndex(const int32 LayoutIndex)
{
	ParentLayoutIndex = LayoutIndex;
}


#undef LOCTEXT_NAMESPACE

