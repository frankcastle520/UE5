// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/Nodes/CustomizableObjectNodeGroupProjectorParameter.h"

#include "MuCO/CustomizableObjectCustomVersion.h"
#include "MuCOE/EdGraphSchema_CustomizableObject.h"
#include "MuCOE/UnrealEditorPortabilityHelpers.h"

class UCustomizableObjectNodeRemapPins;

#define LOCTEXT_NAMESPACE "CustomizableObjectEditor"


TArray<FGroupProjectorParameterImage> UCustomizableObjectNodeGroupProjectorParameter::GetOptionTexturesFromTable() const
{
	TArray<FGroupProjectorParameterImage> ArrayResult;

	if (OptionTexturesDataTable == nullptr)
	{
		return ArrayResult;
	}

	TArray<FName> ArrayRowName = OptionTexturesDataTable->GetRowNames();

	FProperty* PropertyTexturePath = OptionTexturesDataTable->FindTableProperty(DataTableTextureColumnName);

	if (PropertyTexturePath == nullptr)
	{
		UE_LOG(LogMutable, Warning, TEXT("WARNING: No column found with texture path information to load projection textures"));
		return ArrayResult;
	}

	int32 NameIndex = 0;

	for (TMap<FName, uint8*>::TConstIterator RowIt = OptionTexturesDataTable->GetRowMap().CreateConstIterator(); RowIt; ++RowIt)
	{
		uint8* RowData = RowIt.Value();
		FString PropertyValue(TEXT(""));
		PropertyTexturePath->ExportText_InContainer(0, PropertyValue, RowData, RowData, nullptr, PPF_None);
		UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *PropertyValue, nullptr);

		if (Texture == nullptr)
		{
			UE_LOG(LogMutable, Warning, TEXT("WARNING: Unable to load texture %s"), *PropertyValue);
		}
		else
		{
			FGroupProjectorParameterImage GroupProjectorParameterImage;
			GroupProjectorParameterImage.OptionName = ArrayRowName[NameIndex].ToString();
			GroupProjectorParameterImage.OptionTexture = Texture;
			ArrayResult.Add(GroupProjectorParameterImage);
		}

		NameIndex++;
	}

	return ArrayResult;
}


TArray<FGroupProjectorParameterImage> UCustomizableObjectNodeGroupProjectorParameter::GetFinalOptionTexturesNoRepeat() const
{
	TArray<FGroupProjectorParameterImage> ArrayDataTable = GetOptionTexturesFromTable();

	for (int32 i = 0; i < OptionTextures.Num(); ++i)
	{
		bool AlreadyAdded = false;
		for (int32 j = 0; j < ArrayDataTable.Num(); ++j)
		{
			if (OptionTextures[i].OptionName == ArrayDataTable[j].OptionName)
			{
				AlreadyAdded = true;
				break;
			}
		}

		if (!AlreadyAdded)
		{
			ArrayDataTable.Add(OptionTextures[i]);
		}
	}

	return ArrayDataTable;
}


FText UCustomizableObjectNodeGroupProjectorParameter::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (TitleType == ENodeTitleType::ListView || ParameterName.IsEmpty())
	{
		return LOCTEXT("Group_Projector_Parameter", "Group Projector Parameter");
	}
	else if (TitleType == ENodeTitleType::EditableTitle)
	{
		return FText::Format(LOCTEXT("Group_Projector_Parameter_EditableTitle", "{0}"), FText::FromString(ParameterName));
	}
	else
	{
		return FText::Format(LOCTEXT("Group_Projector_Parameter_Title", "{0}\nGroup Projector Parameter"), FText::FromString(ParameterName));
	}
}


FLinearColor UCustomizableObjectNodeGroupProjectorParameter::GetNodeTitleColor() const
{
	const UEdGraphSchema_CustomizableObject* Schema = GetDefault<UEdGraphSchema_CustomizableObject>();
	return Schema->GetPinTypeColor(Schema->PC_GroupProjector);
}


FText UCustomizableObjectNodeGroupProjectorParameter::GetTooltipText() const
{
	return LOCTEXT("Group_Projector_Parameter_Tooltip", "Projects one or many textures to all children in the group it's connected to. It modifies only the materials that define a specific material asset texture parameter.");
}


void UCustomizableObjectNodeGroupProjectorParameter::BackwardsCompatibleFixup(int32 CustomizableObjectCustomVersion)
{
	Super::BackwardsCompatibleFixup(CustomizableObjectCustomVersion);
	
	if (CustomizableObjectCustomVersion == FCustomizableObjectCustomVersion::GroupProjectorPinTypeAdded)
	{
		if (UEdGraphPin* Pin = ProjectorPin())
		{
			Pin->PinType.PinCategory = UEdGraphSchema_CustomizableObject::PC_GroupProjector;
		}
	}

	if (CustomizableObjectCustomVersion == FCustomizableObjectCustomVersion::GroupProjectorImagePinRemoved)
	{
		ReconstructNode();
	}
}


void UCustomizableObjectNodeGroupProjectorParameter::AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins)
{
	const UEdGraphSchema_CustomizableObject* Schema = GetDefault<UEdGraphSchema_CustomizableObject>();

	CustomCreatePin(EGPD_Output, Schema->PC_GroupProjector, TEXT("Value"));	
}


UEdGraphPin& UCustomizableObjectNodeGroupProjectorParameter::OutputPin() const
{
	return *FindPin(TEXT("Value"));
}


#undef LOCTEXT_NAMESPACE
