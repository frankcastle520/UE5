// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/Nodes/CustomizableObjectNodeComponentMeshAddTo.h"

#include "MuCOE/EdGraphSchema_CustomizableObject.h"
#include "MuCOE/Nodes/CustomizableObjectNodeComponentMesh.h"


#define LOCTEXT_NAMESPACE "CustomizableObjectEditor"


void UCustomizableObjectNodeComponentMeshAddTo::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FProperty* PropertyThatChanged = PropertyChangedEvent.Property;
	if (!PropertyThatChanged)
	{
		return;
	}

	if (PropertyThatChanged->GetFName() == GET_MEMBER_NAME_CHECKED(UCustomizableObjectNodeComponentMesh, NumLODs))
	{
		ReconstructNode();
	}
}


FText UCustomizableObjectNodeComponentMeshAddTo::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (TitleType == ENodeTitleType::ListView)
	{
		return LOCTEXT("ComponentMeshAdd", "Add To Mesh Component");
	}
	else
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("ComponentName"), FText::FromName(ParentComponentName));

		return FText::Format(LOCTEXT("ComponentMeshAdd_Title", "{ComponentName}\nAdd To Mesh Component"), Args);
	}
}


#undef LOCTEXT_NAMESPACE

