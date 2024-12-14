// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/Nodes/CustomizableObjectNodeComponentPassthroughMesh.h"

#include "Engine/SkeletalMesh.h"
#include "Modules/ModuleManager.h"
#include "MuCO/CustomizableObjectCustomVersion.h"
#include "MuCOE/CustomizableObjectEditor.h"
#include "MuCOE/CustomizableObjectEditorStyle.h"
#include "MuCOE/CustomizableObjectLayout.h"
#include "MuCOE/EdGraphSchema_CustomizableObject.h"
#include "MuCOE/GraphTraversal.h"
#include "MuCOE/MutableUtils.h"
#include "MuCOE/Nodes/CustomizableObjectNodeLayoutBlocks.h"
#include "MuCOE/RemapPins/CustomizableObjectNodeRemapPinsByNameDefaultPin.h"
#include "MuCOE/UnrealEditorPortabilityHelpers.h"

class UCustomizableObjectNodeRemapPinsByName;
class UObject;


#define LOCTEXT_NAMESPACE "CustomizableObjectEditor"

const FName UCustomizableObjectNodeComponentPassthroughMesh::OutputPinName(TEXT("Component"));


void UCustomizableObjectNodeComponentMeshPinDataSection::Init(int32 InLODIndex, int32 InSectionIndex)
{
	LODIndex = InLODIndex;
	SectionIndex = InSectionIndex;
}


int32 UCustomizableObjectNodeComponentMeshPinDataSection::GetLODIndex() const
{
	return LODIndex;
}


int32 UCustomizableObjectNodeComponentMeshPinDataSection::GetSectionIndex() const
{
	return SectionIndex;
}


bool UCustomizableObjectNodeComponentMeshPinDataSection::Equals(const UCustomizableObjectNodePinData& Other) const
{
	if (GetClass() != Other.GetClass())
	{
		return false;
	}

	const UCustomizableObjectNodeComponentMeshPinDataSection& OtherTyped = static_cast<const UCustomizableObjectNodeComponentMeshPinDataSection&>(Other);
	if (LODIndex != OtherTyped.LODIndex ||
		SectionIndex != OtherTyped.SectionIndex)
	{
		return false;
	}

	return Super::Equals(Other);
}


void UCustomizableObjectNodeComponentPassthroughMesh::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	FProperty* PropertyThatChanged = PropertyChangedEvent.Property;
	if (PropertyThatChanged && PropertyThatChanged->GetName() == TEXT("Mesh"))
	{
		ReconstructNode();
	}
}


void UCustomizableObjectNodeComponentPassthroughMesh::AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins)
{
	const UEdGraphSchema_CustomizableObject* Schema = GetDefault<UEdGraphSchema_CustomizableObject>();
	if (!Schema)
	{
		return;
	}

	UEdGraphPin* OutputPin = CustomCreatePin(EGPD_Output, Schema->PC_Component, OutputPinName);

	if (!Mesh.IsValid())
	{
		return;
	}

	// Support SkeletalMeshes only, for now.
	USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>( Mesh.TryLoad() );
	if (!SkeletalMesh)
	{
		return;
	}
	
	if (const FSkeletalMeshModel* ImportedModel = SkeletalMesh->GetImportedModel())
	{
		const int32 NumLODs = SkeletalMesh->GetLODNum();
		for (int32 LODIndex = 0; LODIndex < NumLODs; ++LODIndex)
		{
			const int32 NumSections = ImportedModel->LODModels[LODIndex].Sections.Num();
			for (int32 SectionIndex = 0; SectionIndex < NumSections; ++SectionIndex)
			{
				// Ignore disabled sections.
				const bool bIsSectionDisabled = ImportedModel ->LODModels[LODIndex].Sections[SectionIndex].bDisabled;
				if (bIsSectionDisabled)
				{
					continue;
				}

				UMaterialInterface* MaterialInterface = GetMaterialInterfaceFor(LODIndex, SectionIndex, ImportedModel);
				
				// Material for the section
				{
					UCustomizableObjectNodeComponentMeshPinDataMaterial* PinData = NewObject<UCustomizableObjectNodeComponentMeshPinDataMaterial>(this);
					PinData->Init(LODIndex, SectionIndex);
					
					FString SectionFriendlyName = MaterialInterface ? MaterialInterface->GetName() : FString::Printf(TEXT("Section %i"), SectionIndex);
					FString MaterialPinName = FString::Printf(TEXT("LOD %i - Section %i - Material"), LODIndex, SectionIndex);

					UEdGraphPin* Pin = CustomCreatePin(EGPD_Input, Schema->PC_Material, FName(*MaterialPinName), PinData);
					Pin->PinFriendlyName = FText::FromString(FString::Printf(TEXT("LOD %i - Section %i - %s"), LODIndex, SectionIndex, *SectionFriendlyName));
					Pin->PinToolTip = MaterialPinName;
				}
				
			}
		}
	}
}


bool UCustomizableObjectNodeComponentPassthroughMesh::IsExperimental() const
{
	return true;
}


FText UCustomizableObjectNodeComponentPassthroughMesh::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (TitleType == ENodeTitleType::ListView)
	{
		return LOCTEXT("ComponentPassthroughMesh", "Passthrough Mesh Component");
	}
	else
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("ComponentName"), FText::FromName(ComponentName));

		return FText::Format(LOCTEXT("ComponentPassthroughMesh_Title", "{ComponentName}\n Passthrough Mesh Component"), Args);
	}
}


FLinearColor UCustomizableObjectNodeComponentPassthroughMesh::GetNodeTitleColor() const
{
	const UEdGraphSchema_CustomizableObject* Schema = GetDefault<UEdGraphSchema_CustomizableObject>();
	return Schema->GetPinTypeColor(Schema->PC_Component);
}


void UCustomizableObjectNodeComponentPassthroughMesh::GetPinSection(const UEdGraphPin& Pin, int32& OutLODIndex, int32& OutSectionIndex) const
{
	if (const UCustomizableObjectNodeComponentMeshPinDataSection* PinData = Cast<UCustomizableObjectNodeComponentMeshPinDataSection>(GetPinData(Pin)))
	{
		OutLODIndex = PinData->GetLODIndex();
		OutSectionIndex = PinData->GetSectionIndex();
		return;
	}

	OutLODIndex = -1;
	OutSectionIndex = -1;
}


UMaterialInterface* UCustomizableObjectNodeComponentPassthroughMesh::GetMaterialFor(const UEdGraphPin* Pin) const
{
	USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Mesh.TryLoad());
	if (SkeletalMesh)
	{
		if (FSkeletalMaterial* SkeletalMaterial = GetSkeletalMaterialFor(*Pin))
		{
			return SkeletalMaterial->MaterialInterface;
		}
	}

	return nullptr;
}


FSkeletalMaterial* UCustomizableObjectNodeComponentPassthroughMesh::GetSkeletalMaterialFor(const UEdGraphPin& Pin) const
{
	int32 LODIndex;
	int32 SectionIndex;
	GetPinSection(Pin, LODIndex, SectionIndex);

	return GetSkeletalMaterialFor(LODIndex, SectionIndex);
}


FText UCustomizableObjectNodeComponentPassthroughMesh::GetTooltipText() const
{
	return LOCTEXT("ComponentPassthroughMesh_Tooltip", "Define a new object component based on a Skeletal Mesh.");
}


UMaterialInterface* UCustomizableObjectNodeComponentPassthroughMesh::GetMaterialInterfaceFor(const int32 LODIndex, const int32 SectionIndex, const FSkeletalMeshModel* ImportedModel) const
{
	if (FSkeletalMaterial* SkeletalMaterial = GetSkeletalMaterialFor(LODIndex, SectionIndex, ImportedModel))
	{
		return SkeletalMaterial->MaterialInterface;
	}

	return nullptr;
}


FSkeletalMaterial* UCustomizableObjectNodeComponentPassthroughMesh::GetSkeletalMaterialFor(const int32 LODIndex, const int32 SectionIndex, const FSkeletalMeshModel* ImportedModel) const
{
	USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Mesh.TryLoad());
	if (!SkeletalMesh)
	{
		return nullptr;
	}

	// We assume that LODIndex and MaterialIndex are valid for the imported model
	int32 SkeletalMeshMaterialIndex = INDEX_NONE;
	
	// Check if we have lod info map to get the correct material index
	if (const FSkeletalMeshLODInfo* LodInfo = SkeletalMesh->GetLODInfo(LODIndex))
	{
		if (LodInfo->LODMaterialMap.IsValidIndex(SectionIndex))
		{
			SkeletalMeshMaterialIndex = LodInfo->LODMaterialMap[SectionIndex];
		}
	}

	// Only deduce index when the explicit mapping is not found or there is no remap
	if (SkeletalMeshMaterialIndex == INDEX_NONE)
	{
	if (ImportedModel && ImportedModel->LODModels.IsValidIndex(LODIndex) && ImportedModel->LODModels[LODIndex].Sections.IsValidIndex(SectionIndex))
	{
		SkeletalMeshMaterialIndex = ImportedModel->LODModels[LODIndex].Sections[SectionIndex].MaterialIndex;
	}
	else
	{
		FSkeletalMeshModel* AuxImportedModel = SkeletalMesh->GetImportedModel();

		if (AuxImportedModel)
		{
			if (AuxImportedModel->LODModels.IsValidIndex(LODIndex) && AuxImportedModel->LODModels[LODIndex].Sections.IsValidIndex(SectionIndex))
			{
				SkeletalMeshMaterialIndex = AuxImportedModel->LODModels[LODIndex].Sections[SectionIndex].MaterialIndex;
			}
		}
	}
	}
	
	if (SkeletalMesh->GetMaterials().IsValidIndex(SkeletalMeshMaterialIndex))
	{
		return &SkeletalMesh->GetMaterials()[SkeletalMeshMaterialIndex];
	}
	
	return nullptr;
}



UEdGraphPin* UCustomizableObjectNodeComponentPassthroughMesh::GetMaterialPin(const int32 LODIndex, const int32 SectionIndex) const
{
	for (UEdGraphPin* Pin : GetAllNonOrphanPins())
	{
		if (const UCustomizableObjectNodeComponentMeshPinDataSection* PinData = Cast<UCustomizableObjectNodeComponentMeshPinDataSection>(GetPinData(*Pin)))
		{
			if (PinData->GetLODIndex() == LODIndex &&
				PinData->GetSectionIndex() == SectionIndex)
			{
				return Pin;
			}
		}
	}

	return nullptr;
}


#undef LOCTEXT_NAMESPACE
