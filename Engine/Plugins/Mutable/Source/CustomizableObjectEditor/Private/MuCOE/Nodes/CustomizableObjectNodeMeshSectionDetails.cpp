// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/Nodes/CustomizableObjectNodeMeshSectionDetails.h"

#include "MuCOE/CustomizableObjectEditorUtilities.h"
#include "MuCOE/CustomizableObjectEditorStyle.h"
#include "MuCOE/GraphTraversal.h"
#include "MuCOE/Nodes/CustomizableObjectNodeMaterial.h"
#include "MuCOE/Nodes/CustomizableObjectNodeSkeletalMesh.h"
#include "MuCOE/UnrealEditorPortabilityHelpers.h"
#include "PropertyCustomizationHelpers.h"
#include "Widgets/Input/STextComboBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SButton.h"
#include "DetailLayoutBuilder.h"
#include "IDetailsView.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/Attribute.h"


#define LOCTEXT_NAMESPACE "CustomizableObjectDetails"


TSharedRef<IDetailCustomization> FCustomizableObjectNodeMeshSectionDetails::MakeInstance()
{
	return MakeShareable( new FCustomizableObjectNodeMeshSectionDetails );
}


void FCustomizableObjectNodeMeshSectionDetails::CustomizeDetails( IDetailLayoutBuilder& DetailBuilder )
{
	FCustomizableObjectNodeDetails::CustomizeDetails(DetailBuilder);

	const IDetailsView* DetailsView = DetailBuilder.GetDetailsView();
	if ( DetailsView->GetSelectedObjects().Num() )
	{
		Node = Cast<UCustomizableObjectNodeMaterial>( DetailsView->GetSelectedObjects()[0].Get() );
	}

	if (!Node)
	{
		return;
	}
	
	// Move tags to enable higher.
	IDetailCategoryBuilder& TagsCategory = DetailBuilder.EditCategory("Tags");
	TagsCategory.SetSortOrder(-5000);

	// Add the required tags widget
	{
		TagsPropertyHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UCustomizableObjectNodeMaterial, Tags), UCustomizableObjectNodeMaterial::StaticClass());
		DetailBuilder.HideProperty(TagsPropertyHandle);

		TagsPropertyHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FCustomizableObjectNodeMeshSectionDetails::OnEnableTagsPropertyChanged));
		TagsPropertyHandle->SetOnChildPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FCustomizableObjectNodeMeshSectionDetails::OnEnableTagsPropertyChanged));

		TagsCategory.AddCustomRow(FText::FromString(TEXT("Enable Tags")))
			.PropertyHandleList({ TagsPropertyHandle })
			.NameContent()
				.VAlign(VAlign_Fill)
			[
				SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.VAlign(VAlign_Top)
					.Padding(FMargin(0,4.0f,0,4.0f))
					[
						SNew(STextBlock)
							.Text(LOCTEXT("MeshSectionDetails_Tags", "Enable Tags"))
							.Font(IDetailLayoutBuilder::GetDetailFont())
					]
			]
			.ValueContent()
				.HAlign(HAlign_Fill)
			[
				SAssignNew(this->TagListWidget, SMutableTagListWidget)
					.Node(Node)
					.TagArray(&Node->Tags)
					.AllowInternalTags(false)
					.EmptyListText(LOCTEXT("MeshSectionDetails_NoTags", "No tags enabled by this mesh section."))
					.OnTagListChanged( this, &FCustomizableObjectNodeMeshSectionDetails::OnEnableTagsPropertyChanged)
			];
	}
}


void FCustomizableObjectNodeMeshSectionDetails::OnEnableTagsPropertyChanged()
{
	// This seems necessary to detect the "Reset to default" actions.
	TagListWidget->RefreshOptions();
	Node->Modify();
}


#undef LOCTEXT_NAMESPACE
