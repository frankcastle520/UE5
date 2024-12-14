// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/Nodes/CustomizableObjectNodeModifierExtendMeshSectionDetails.h"

#include "MuCOE/CustomizableObjectEditorUtilities.h"
#include "MuCOE/CustomizableObjectEditorStyle.h"
#include "MuCOE/GraphTraversal.h"
#include "MuCOE/Nodes/CustomizableObjectNodeMaterial.h"
#include "MuCOE/Nodes/CustomizableObjectNodeModifierExtendMeshSection.h"
#include "MuCOE/Nodes/CustomizableObjectNodeSkeletalMesh.h"
#include "MuCOE/UnrealEditorPortabilityHelpers.h"
#include "PropertyCustomizationHelpers.h"
#include "Widgets/Input/STextComboBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "DetailLayoutBuilder.h"
#include "IDetailsView.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/Attribute.h"


#define LOCTEXT_NAMESPACE "CustomizableObjectDetails"


TSharedRef<IDetailCustomization> FCustomizableObjectNodeModifierExtendMeshSectionDetails::MakeInstance()
{
	return MakeShareable( new FCustomizableObjectNodeModifierExtendMeshSectionDetails );
}


void FCustomizableObjectNodeModifierExtendMeshSectionDetails::CustomizeDetails( IDetailLayoutBuilder& DetailBuilder )
{
	FCustomizableObjectNodeModifierBaseDetails::CustomizeDetails(DetailBuilder);

	const IDetailsView* DetailsView = DetailBuilder.GetDetailsView();
	if ( DetailsView->GetSelectedObjects().Num() )
	{
		Node = Cast<UCustomizableObjectNodeModifierExtendMeshSection>( DetailsView->GetSelectedObjects()[0].Get() );
	}

	if (!Node)
	{
		return;
	}

	// Move tags to enable higher.
	IDetailCategoryBuilder& TagsCategory = DetailBuilder.EditCategory("EnableTags");
	TagsCategory.SetSortOrder(-5000);

	// Add the required tags widget
	{
		EnableTagsPropertyHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UCustomizableObjectNodeModifierExtendMeshSection, Tags), UCustomizableObjectNodeModifierExtendMeshSection::StaticClass());
		DetailBuilder.HideProperty(EnableTagsPropertyHandle);

		EnableTagsPropertyHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FCustomizableObjectNodeModifierExtendMeshSectionDetails::OnRequiredTagsPropertyChanged));
		EnableTagsPropertyHandle->SetOnChildPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FCustomizableObjectNodeModifierExtendMeshSectionDetails::OnRequiredTagsPropertyChanged));

		TagsCategory.AddCustomRow(FText::FromString(TEXT("Enable Tags")))
			.PropertyHandleList({ EnableTagsPropertyHandle })
			.NameContent()
			.VAlign(VAlign_Fill)
			[
				SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.VAlign(VAlign_Top)
					.Padding(FMargin(0, 4.0f, 0, 4.0f))
					[
						SNew(STextBlock)
							.Text(LOCTEXT("ExtendMeshSectionDetails_Tags", "Tags enabled for extended data"))
							.Font(IDetailLayoutBuilder::GetDetailFont())
					]
			]
			.ValueContent()
			.HAlign(HAlign_Fill)
			[
				SAssignNew(this->EnableTagListWidget, SMutableTagListWidget)
					.Node(Node)
					.TagArray(&Node->Tags)
					.AllowInternalTags(false)
					.EmptyListText(LOCTEXT("ExtendMeshSectionDetails_NoTags", "No tags enabled by this extended mesh section."))
					.OnTagListChanged(this, &FCustomizableObjectNodeModifierExtendMeshSectionDetails::OnEnableTagsPropertyChanged)
			];
	}

}


void FCustomizableObjectNodeModifierExtendMeshSectionDetails::OnEnableTagsPropertyChanged()
{
	// This seems necessary to detect the "Reset to default" actions.
	EnableTagListWidget->RefreshOptions();
	Node->Modify();
}


#undef LOCTEXT_NAMESPACE
