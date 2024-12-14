// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/Widgets/Editor/SDMObjectEditorWidgetBase.h"

#include "CustomDetailsViewArgs.h"
#include "CustomDetailsViewModule.h"
#include "DetailLayoutBuilder.h"
#include "DMWorldSubsystem.h"
#include "Engine/World.h"
#include "ICustomDetailsView.h"
#include "Items/ICustomDetailsViewCustomCategoryItem.h"
#include "Items/ICustomDetailsViewCustomItem.h"
#include "Items/ICustomDetailsViewItem.h"
#include "Model/DynamicMaterialModelBase.h"
#include "UI/Utils/DMWidgetStatics.h"
#include "UI/Widgets/SDMMaterialEditor.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SDMObjectEditorWidgetBase"

void SDMObjectEditorWidgetBase::PrivateRegisterAttributes(FSlateAttributeDescriptor::FInitializer&)
{
}

SDMObjectEditorWidgetBase::~SDMObjectEditorWidgetBase()
{
	FDMWidgetStatics::Get().ClearPropertyHandles(this);
}

void SDMObjectEditorWidgetBase::Construct(const FArguments& InArgs, const TSharedRef<SDMMaterialEditor>& InEditorWidget, UObject* InObject)
{
	EditorWidgetWeak = InEditorWidget;
	ObjectWeak = InObject;
	bConstructing = false;
	KeyframeHandler = nullptr;

	TGuardValue<bool> Constructing = TGuardValue<bool>(bConstructing, true);

	UObject* WorldContext = InObject;

	if (!WorldContext)
	{
		WorldContext = InEditorWidget->GetMaterialModelBase();
	}

	if (WorldContext)
	{
		if (const UWorld* const World = WorldContext->GetWorld())
		{
			if (const UDMWorldSubsystem* const WorldSubsystem = World->GetSubsystem<UDMWorldSubsystem>())
			{
				KeyframeHandler = WorldSubsystem->GetKeyframeHandler();
			}
		}
	}

	SScrollBox::FSlot* ContentSlotPtr = nullptr;

	ChildSlot
	[
		SNew(SScrollBox)
		+ SScrollBox::Slot()
		.Expose(ContentSlotPtr)
		.VAlign(EVerticalAlignment::VAlign_Fill)
		[
			SNullWidget::NullWidget
		]		
	];

	ContentSlot = TDMWidgetSlot<SWidget>(ContentSlotPtr, CreateWidget());
}

void SDMObjectEditorWidgetBase::Validate()
{
	if (!ObjectWeak.IsValid())
	{
		ContentSlot.ClearWidget();
	}
}

TSharedRef<SWidget> SDMObjectEditorWidgetBase::CreateWidget()
{
	FDMWidgetStatics::Get().ClearPropertyHandles(this);

	UObject* Object = ObjectWeak.Get();

	FCustomDetailsViewArgs Args;
	Args.KeyframeHandler = KeyframeHandler;
	Args.bAllowGlobalExtensions = true;
	Args.bAllowResetToDefault = true;
	Args.bShowCategories = false;
	Args.OnExpansionStateChanged.AddSP(this, &SDMObjectEditorWidgetBase::OnExpansionStateChanged);

	TSharedRef<ICustomDetailsView> DetailsView = ICustomDetailsViewModule::Get().CreateCustomDetailsView(Args);
	FCustomDetailsViewItemId RootId = DetailsView->GetRootItem()->GetItemId();

	TArray<FDMPropertyHandle> PropertyRows = GetPropertyRows();

	for (const FDMPropertyHandle& PropertyRow : PropertyRows)
	{
		const bool bHasValidCustomWidget = PropertyRow.ValueWidget.IsValid() && !PropertyRow.ValueName.IsNone() && PropertyRow.NameOverride.IsSet();

		if (!PropertyRow.DetailTreeNode && !bHasValidCustomWidget)
		{
			continue;
		}

		ECustomDetailsTreeInsertPosition Position;

		switch (PropertyRow.Priority)
		{
			case EDMPropertyHandlePriority::High:
				Position = ECustomDetailsTreeInsertPosition::FirstChild;
				break;

			case EDMPropertyHandlePriority::Low:
				Position = ECustomDetailsTreeInsertPosition::LastChild;
				break;

			default:
				Position = ECustomDetailsTreeInsertPosition::Child;
				break;
		}

		TSharedRef<ICustomDetailsViewItem> CategoryItem = GetCategoryForRow(DetailsView, RootId, PropertyRow);

		if (bHasValidCustomWidget)
		{
			AddCustomRow(DetailsView, CategoryItem->GetItemId(), Position, PropertyRow);
		}
		else if (PropertyRow.DetailTreeNode)
		{
			AddDetailTreeRow(DetailsView, CategoryItem->GetItemId(), Position, PropertyRow);
		}		
	}

	DetailsView->RebuildTree(ECustomDetailsViewBuildType::InstantBuild);

	return DetailsView;
}

TSharedRef<ICustomDetailsViewItem> SDMObjectEditorWidgetBase::GetDefaultCategory(const TSharedRef<ICustomDetailsView>& InDetailsView,
	const FCustomDetailsViewItemId& InRootId)
{
	if (!DefaultCategoryItem.IsValid())
	{
		DefaultCategoryItem = InDetailsView->CreateCustomCategoryItem(DefaultCategoryName, LOCTEXT("General", "General"))->AsItem();
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

TSharedRef<ICustomDetailsViewItem> SDMObjectEditorWidgetBase::GetCategoryForRow(const TSharedRef<ICustomDetailsView>& InDetailsView,
	const FCustomDetailsViewItemId& InRootId, const FDMPropertyHandle& InPropertyRow)
{
	FName CategoryName = InPropertyRow.CategoryOverrideName;

	if (CategoryName.IsNone() && InPropertyRow.PropertyHandle.IsValid())
	{
		// Sub category (possibly)
		if (TSharedPtr<IPropertyHandle> SubCategoryProperty = InPropertyRow.PropertyHandle->GetParentHandle())
		{
			if (SubCategoryProperty->IsCategoryHandle())
			{
				// "Material Designer" (possibly)
				if (TSharedPtr<IPropertyHandle> MaterialDesignerCategoryProperty = SubCategoryProperty->GetParentHandle())
				{
					if (MaterialDesignerCategoryProperty->IsCategoryHandle())
					{
						CategoryName = *SubCategoryProperty->GetPropertyDisplayName().ToString();
					}
				}
			}
		}
	}

	if (CategoryName.IsNone())
	{
		return GetDefaultCategory(InDetailsView, InRootId);
	}

	TSharedPtr<ICustomDetailsViewItem> CategoryItem = InDetailsView->FindCustomItem(CategoryName);

	if (CategoryItem.IsValid())
	{
		return CategoryItem.ToSharedRef();
	}

	CategoryItem = InDetailsView->CreateCustomCategoryItem(CategoryName, FText::FromName(CategoryName))->AsItem();
	CategoryItem->RefreshItemId();
	InDetailsView->ExtendTree(InRootId, ECustomDetailsTreeInsertPosition::Child, CategoryItem.ToSharedRef());

	bool bExpansionState = true;
	FDMWidgetStatics::Get().GetExpansionState(ObjectWeak.Get(), CategoryName, bExpansionState);

	InDetailsView->SetItemExpansionState(
		CategoryItem->GetItemId(),
		bExpansionState ? ECustomDetailsViewExpansion::SelfExpanded : ECustomDetailsViewExpansion::Collapsed
	);

	Categories.Add(CategoryName);

	return CategoryItem.ToSharedRef();
}

void SDMObjectEditorWidgetBase::AddDetailTreeRow(const TSharedRef<ICustomDetailsView>& InDetailsView,
	const FCustomDetailsViewItemId& InParentId, ECustomDetailsTreeInsertPosition InPosition, const FDMPropertyHandle& InPropertyRow)
{
	TSharedRef<ICustomDetailsViewItem> Item = InDetailsView->CreateDetailTreeItem(InPropertyRow.DetailTreeNode.ToSharedRef());

	if (InPropertyRow.NameOverride.IsSet())
	{
		Item->SetOverrideWidget(
			ECustomDetailsViewWidgetType::Name,
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.Text(InPropertyRow.NameOverride.GetValue())
			.ToolTipText(InPropertyRow.NameToolTipOverride.Get(FText::GetEmpty()))
		);
	}

	if (!InPropertyRow.bEnabled)
	{
		Item->SetEnabledOverride(false);

		// Disable the expansion widgets (SNullWidget is treated as removing the override).
		Item->SetOverrideWidget(ECustomDetailsViewWidgetType::Extensions, SNew(SBox));
	}

	if (!InPropertyRow.bKeyframeable)
	{
		Item->SetKeyframeEnabled(false);
	}

	if (InPropertyRow.ResetToDefaultOverride.IsSet())
	{
		Item->SetResetToDefaultOverride(InPropertyRow.ResetToDefaultOverride.GetValue());
	}

	if (InPropertyRow.MaxWidth.IsSet())
	{
		Item->SetValueWidgetWidthOverride(InPropertyRow.MaxWidth);
	}

	InDetailsView->ExtendTree(InParentId, InPosition, Item);
}

void SDMObjectEditorWidgetBase::AddCustomRow(const TSharedRef<ICustomDetailsView>& InDetailsView, 
	const FCustomDetailsViewItemId& InParentId, ECustomDetailsTreeInsertPosition InPosition, const FDMPropertyHandle& InPropertyRow)
{
	TSharedPtr<ICustomDetailsViewCustomItem> Item = InDetailsView->CreateCustomItem(
		InPropertyRow.ValueName,
		InPropertyRow.NameOverride.GetValue(),
		InPropertyRow.NameToolTipOverride.Get(FText::GetEmpty())
	);

	if (!Item.IsValid())
	{
		return;
	}

	Item->SetValueWidget(InPropertyRow.ValueWidget.ToSharedRef());

	if (!InPropertyRow.bEnabled)
	{
		Item->AsItem()->SetEnabledOverride(false);

		// Disable the expansion widgets (SNullWidget is treated as removing the override).
		Item->SetExpansionWidget(SNew(SBox));
	}

	if (InPropertyRow.MaxWidth.IsSet())
	{
		Item->AsItem()->SetValueWidgetWidthOverride(InPropertyRow.MaxWidth);
	}

	InDetailsView->ExtendTree(InParentId, InPosition, Item->AsItem());
}

void SDMObjectEditorWidgetBase::OnExpansionStateChanged(const TSharedRef<ICustomDetailsViewItem>& InItem, bool bInExpansionState)
{
	if (bConstructing)
	{
		return;
	}

	const FCustomDetailsViewItemId& ItemId = InItem->GetItemId();

	if (ItemId.GetItemType() != static_cast<uint32>(EDetailNodeType::Category))
	{
		return;
	}

	FDMWidgetStatics::Get().SetExpansionState(ObjectWeak.Get(), *ItemId.GetItemName(), bInExpansionState);
}

#undef LOCTEXT_NAMESPACE
