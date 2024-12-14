// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/Widgets/Editor/SDMMaterialComponentEditor.h"

#include "Components/DMMaterialComponent.h"
#include "Components/DMMaterialLayer.h"
#include "CustomDetailsViewArgs.h"
#include "DynamicMaterialEditorModule.h"
#include "DynamicMaterialModule.h"
#include "ICustomDetailsView.h"
#include "Items/ICustomDetailsViewCustomCategoryItem.h"
#include "Items/ICustomDetailsViewItem.h"
#include "UI/Utils/DMWidgetStatics.h"
#include "UI/Widgets/SDMMaterialEditor.h"
#include "Widgets/Input/SComboButton.h"

#define LOCTEXT_NAMESPACE "SDMMaterialComponentEditor"

void SDMMaterialComponentEditor::PrivateRegisterAttributes(FSlateAttributeDescriptor::FInitializer&)
{
}

SDMMaterialComponentEditor::~SDMMaterialComponentEditor()
{
	if (!FDynamicMaterialModule::AreUObjectsSafe())
	{
		return;
	}

	if (UDMMaterialComponent* Component = GetComponent())
	{
		Component->GetOnUpdate().RemoveAll(this);
	}
}

void SDMMaterialComponentEditor::Construct(const FArguments& InArgs, const TSharedRef<SDMMaterialEditor>& InEditorWidget, 
	UDMMaterialComponent* InMaterialComponent)
{
	SetCanTick(false);

	SDMObjectEditorWidgetBase::Construct(
		SDMObjectEditorWidgetBase::FArguments(), 
		InEditorWidget, 
		InMaterialComponent
	);

	if (InMaterialComponent)
	{
		InMaterialComponent->GetOnUpdate().AddSP(this, &SDMMaterialComponentEditor::OnComponentUpdated);
	}
}

UDMMaterialComponent* SDMMaterialComponentEditor::GetComponent() const
{
	return Cast<UDMMaterialComponent>(ObjectWeak.Get());
}

void SDMMaterialComponentEditor::OnComponentUpdated(UDMMaterialComponent* InComponent, UDMMaterialComponent* InSource, EDMUpdateType InUpdateType)
{
	if (EnumHasAnyFlags(InUpdateType, EDMUpdateType::Structure))
	{
		if (TSharedPtr<SDMMaterialEditor> EditorWidget = GetEditorWidget())
		{
			EditorWidget->EditComponent(GetComponent(), /* Force refresh */ true);
		}
	}
}

TSharedRef<ICustomDetailsViewItem> SDMMaterialComponentEditor::GetDefaultCategory(const TSharedRef<ICustomDetailsView>& InDetailsView,
	const FCustomDetailsViewItemId& InRootId)
{
	UDMMaterialComponent* Component = GetComponent();

	if (!Component)
	{
		return SDMObjectEditorWidgetBase::GetDefaultCategory(InDetailsView, InRootId);
	}

	if (!DefaultCategoryItem.IsValid())
	{
		const FText ComponentCategoryFormat = LOCTEXT("ComponantCategoryFormat", "{0} Settings");
		const FText ComponentCategoryText = FText::Format(ComponentCategoryFormat, Component->GetComponentDescription());
		DefaultCategoryItem = InDetailsView->CreateCustomCategoryItem(DefaultCategoryName, ComponentCategoryText)->AsItem();
		DefaultCategoryItem->RefreshItemId();
		InDetailsView->ExtendTree(InRootId, ECustomDetailsTreeInsertPosition::Child, DefaultCategoryItem.ToSharedRef());

		bool bExpansionState = true;
		FDMWidgetStatics::Get().GetExpansionState(ObjectWeak.Get(), DefaultCategoryName, bExpansionState);

		InDetailsView->SetItemExpansionState(
			DefaultCategoryItem->GetItemId(),
			bExpansionState ? ECustomDetailsViewExpansion::SelfExpanded : ECustomDetailsViewExpansion::Collapsed
		);

		Categories.Add(DefaultCategoryName);
	}

	return DefaultCategoryItem.ToSharedRef();
}

TArray<FDMPropertyHandle> SDMMaterialComponentEditor::GetPropertyRows()
{
	TArray<FDMPropertyHandle> PropertyRows;
	TSet<UDMMaterialComponent*> ProcessedObjects;

	FDynamicMaterialEditorModule::GeneratorComponentPropertyRows(
		SharedThis(this),
		GetComponent(),
		PropertyRows,
		ProcessedObjects
	);

	return PropertyRows;
}

void SDMMaterialComponentEditor::OnUndo()
{
	if (TSharedPtr<SDMMaterialEditor> EditorWidget = GetEditorWidget())
	{
		EditorWidget->EditComponent(GetComponent(), /* Force refresh */ true);
	}
}

#undef LOCTEXT_NAMESPACE
