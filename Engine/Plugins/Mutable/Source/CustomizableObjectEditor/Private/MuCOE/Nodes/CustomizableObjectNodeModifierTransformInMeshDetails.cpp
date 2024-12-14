// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/Nodes/CustomizableObjectNodeModifierTransformInMeshDetails.h"

#include "MuCOE/Nodes/CustomizableObjectNodeModifierTransformInMesh.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"

#define LOCTEXT_NAMESPACE "CustomizableObjectNodeModifierTransformInMeshDetails"


TSharedRef<IDetailCustomization> FCustomizableObjectNodeModifierTransformInMeshDetails::MakeInstance()
{
	return MakeShareable(new FCustomizableObjectNodeModifierTransformInMeshDetails);
}


void FCustomizableObjectNodeModifierTransformInMeshDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	FCustomizableObjectNodeModifierBaseDetails::CustomizeDetails(DetailBuilder);

	Node = nullptr;
	DetailBuilderPtr = &DetailBuilder;

	const IDetailsView* DetailsView = DetailBuilder.GetDetailsView();
	if (DetailsView->GetSelectedObjects().Num())
	{
		Node = Cast<UCustomizableObjectNodeModifierTransformInMesh>(DetailsView->GetSelectedObjects()[0].Get());
	}

	IDetailCategoryBuilder& CustomizableObjectToClipCategory = DetailBuilder.EditCategory("BoundingMesh");

	if(!Node)
	{
		CustomizableObjectToClipCategory.AddCustomRow(LOCTEXT("Node", "Node"))
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Node not found", "Node not found"))
			];
	}
}

#undef LOCTEXT_NAMESPACE
