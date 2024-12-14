// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/Nodes/CustomizableObjectNodeComponentMesh.h"

#include "MuCO/CustomizableObjectCustomVersion.h"
#include "MuCOE/EdGraphSchema_CustomizableObject.h"
#include "MuCOE/GraphTraversal.h"


#define LOCTEXT_NAMESPACE "CustomizableObjectEditor"


void UCustomizableObjectNodeComponentMesh::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FProperty* PropertyThatChanged = PropertyChangedEvent.Property;
	if (!PropertyThatChanged)
	{
		return;
	}

	if (PropertyThatChanged->GetFName() == GET_MEMBER_NAME_CHECKED(UCustomizableObjectNodeComponentMesh, NumLODs))
	{
		LODReductionSettings.SetNum(NumLODs);
		
		ReconstructNode();
	}
}


void UCustomizableObjectNodeComponentMesh::BackwardsCompatibleFixup(int32 CustomizableObjectCustomVersion)
{
	Super::BackwardsCompatibleFixup(CustomizableObjectCustomVersion);
	
	if (CustomizableObjectCustomVersion == FCustomizableObjectCustomVersion::ComponentsArray)
	{
		UCustomizableObject* Object = GetRootObject(*this);

		if (FMutableMeshComponentData* Result = Object->GetPrivate()->MutableMeshComponents_DEPRECATED.FindByPredicate([&](const FMutableMeshComponentData& ComponentData)
		{
			return ComponentData.Name == ComponentName;
		}))
		{
			ReferenceSkeletalMesh = Result->ReferenceSkeletalMesh;
		}
	}
}


FText UCustomizableObjectNodeComponentMesh::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (TitleType == ENodeTitleType::ListView)
	{
		return LOCTEXT("Component_Mesh", "Mesh Component");
	}
	else
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("ComponentName"), FText::FromName(ComponentName));

		return FText::Format(LOCTEXT("ComponentMesh_Title", "{ComponentName}\nMesh Component"), Args);
	}
}


#undef LOCTEXT_NAMESPACE

