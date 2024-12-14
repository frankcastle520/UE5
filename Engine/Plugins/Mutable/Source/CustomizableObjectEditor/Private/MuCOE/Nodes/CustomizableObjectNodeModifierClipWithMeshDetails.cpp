// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/Nodes/CustomizableObjectNodeModifierClipWithMeshDetails.h"

#include "DetailLayoutBuilder.h"
#include "IDetailsView.h"
#include "MuCOE/CustomizableObjectEditorUtilities.h"
#include "MuCOE/GraphTraversal.h"
#include "MuCOE/UnrealEditorPortabilityHelpers.h"
#include "MuCOE/Nodes/CustomizableObjectNodeMaterial.h"
#include "MuCOE/Nodes/CustomizableObjectNodeModifierClipWithMesh.h"
#include "MuCOE/Nodes/CustomizableObjectNodeStaticMesh.h"
#include "PropertyCustomizationHelpers.h"
#include "MuCO/CustomizableObjectPrivate.h"

class IPropertyHandle;


#define LOCTEXT_NAMESPACE "CustomizableObjectNodeModifierClipWithMeshDetails"


TSharedRef<IDetailCustomization> FCustomizableObjectNodeModifierClipWithMeshDetails::MakeInstance()
{
	return MakeShareable(new FCustomizableObjectNodeModifierClipWithMeshDetails);
}


void FCustomizableObjectNodeModifierClipWithMeshDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	FCustomizableObjectNodeModifierBaseDetails::CustomizeDetails(DetailBuilder);

	Node = nullptr;
	DetailBuilderPtr = &DetailBuilder;

	const IDetailsView* DetailsView = DetailBuilder.GetDetailsView();
	if (DetailsView->GetSelectedObjects().Num())
	{
		Node = Cast<UCustomizableObjectNodeModifierClipWithMesh>(DetailsView->GetSelectedObjects()[0].Get());
	}

	IDetailCategoryBuilder& CustomizableObjectToClipCategory = DetailBuilder.EditCategory("ClipMesh");

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
