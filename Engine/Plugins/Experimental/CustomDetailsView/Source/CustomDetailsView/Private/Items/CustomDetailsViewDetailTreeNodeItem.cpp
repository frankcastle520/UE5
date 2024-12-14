// Copyright Epic Games, Inc. All Rights Reserved.

#include "CustomDetailsViewDetailTreeNodeItem.h"
#include "CustomDetailsViewSequencer.h"
#include "DetailColumnSizeData.h"
#include "DetailRowMenuContext.h"
#include "DetailTreeNode.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IDetailKeyframeHandler.h"
#include "IDetailPropertyRow.h"
#include "IDetailTreeNode.h"
#include "Modules/ModuleManager.h"
#include "ScopedTransaction.h"
#include "SCustomDetailsView.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "Widgets/Input/SCheckBox.h"

#define LOCTEXT_NAMESPACE "CustomDetailsViewItem"

FCustomDetailsViewDetailTreeNodeItem::FCustomDetailsViewDetailTreeNodeItem(const TSharedRef<SCustomDetailsView>& InCustomDetailsView
		, const TSharedPtr<ICustomDetailsViewItem>& InParentItem
		, const TSharedPtr<IDetailTreeNode>& InDetailTreeNode)
	: FCustomDetailsViewItemBase(InCustomDetailsView, InParentItem)
	, DetailTreeNodeWeak(InDetailTreeNode)
{
	if (InDetailTreeNode.IsValid())
	{
		PropertyHandle = InDetailTreeNode->CreatePropertyHandle();
		NodeType = InDetailTreeNode->GetNodeType();
		InitWidget(InDetailTreeNode.ToSharedRef());
	}
}

FCustomDetailsViewDetailTreeNodeItem::~FCustomDetailsViewDetailTreeNodeItem()
{
	if (FSlateApplication::IsInitialized() && UpdateResetToDefaultHandle.IsValid())
	{
		FSlateApplication::Get().OnPostTick().Remove(UpdateResetToDefaultHandle);
	}
}

void FCustomDetailsViewDetailTreeNodeItem::RefreshItemId()
{
	if (TSharedPtr<IDetailTreeNode> DetailTreeNode = DetailTreeNodeWeak.Pin())
	{
		ItemId = FCustomDetailsViewItemId::MakeFromDetailTreeNode(DetailTreeNode.ToSharedRef());
	}
	else
	{
		ItemId = FCustomDetailsViewItemId();
	}
}

void FCustomDetailsViewDetailTreeNodeItem::InitWidget(const TSharedRef<IDetailTreeNode>& InDetailTreeNode)
{
	if (!FSlateApplication::IsInitialized())
	{
		return;
	}

	const FDetailTreeNode* const DetailTreeNode = static_cast<FDetailTreeNode*>(&InDetailTreeNode.Get());
	DetailTreeNode->GenerateStandaloneWidget(DetailWidgetRow);

	const TAttribute<bool> CanEditPropertyAttribute = PropertyHandle.IsValid()
		? DetailTreeNode->IsPropertyEditingEnabled()
		: TAttribute<bool>();

	const TAttribute<bool> EditConditionAttribute   = DetailWidgetRow.EditConditionValue;
	const TAttribute<bool> RowEnabledAttribute      = DetailWidgetRow.IsEnabledAttr;
	const TAttribute<bool> RowValueEnabledAttribute = DetailWidgetRow.IsValueEnabledAttr;

	const TAttribute<bool> IsEnabledAttribute = TAttribute<bool>::CreateLambda(
		[CanEditPropertyAttribute, RowEnabledAttribute, EditConditionAttribute]()
		{
			return CanEditPropertyAttribute.Get(true)
				&& RowEnabledAttribute.Get(true)
				&& EditConditionAttribute.Get(true);
		});

	const TAttribute<bool> IsValueEnabledAttribute = TAttribute<bool>::CreateLambda(
		[IsEnabledAttribute, RowValueEnabledAttribute]()
		{
			return IsEnabledAttribute.Get()
				&& RowValueEnabledAttribute.Get(true);
		});

	DetailWidgetRow.NameWidget.Widget->SetEnabled(IsEnabledAttribute);
	DetailWidgetRow.ValueWidget.Widget->SetEnabled(IsValueEnabledAttribute);
	DetailWidgetRow.ExtensionWidget.Widget->SetEnabled(IsEnabledAttribute);
}

IDetailsView* FCustomDetailsViewDetailTreeNodeItem::GetDetailsView() const
{
	if (const TSharedPtr<IDetailTreeNode> DetailTreeNode = GetRowTreeNode())
	{
		return DetailTreeNode->GetNodeDetailsView();
	}

	TSharedPtr<ICustomDetailsViewItem> ParentItem = GetParent();

	while (ParentItem.IsValid())
	{
		if (IDetailsView* DetailsView = ParentItem->GetDetailsView())
		{
			return DetailsView;
		}

		ParentItem = ParentItem->GetParent();
	}

	return nullptr;
}

void FCustomDetailsViewDetailTreeNodeItem::SetResetToDefaultOverride(const FResetToDefaultOverride& InOverride)
{
	DetailWidgetRow.CustomResetToDefault = InOverride;
}

void FCustomDetailsViewDetailTreeNodeItem::AddExtensionWidget(const TSharedRef<SSplitter>& InSplitter
	, const FDetailColumnSizeData& InColumnSizeData
	, const FCustomDetailsViewArgs& InViewArgs)
{
	TArray<FPropertyRowExtensionButton> ExtensionButtons;

	// Reset to Default
	if (InViewArgs.bAllowResetToDefault)
	{
		FPropertyRowExtensionButton& ResetToDefault = ExtensionButtons.AddDefaulted_GetRef();

		ResetToDefault.Label   = LOCTEXT("ResetToDefault", "Reset to Default");
		ResetToDefault.ToolTip = TAttribute<FText>::CreateSP(this, &FCustomDetailsViewDetailTreeNodeItem::GetResetToDefaultToolTip);
		ResetToDefault.Icon    = TAttribute<FSlateIcon>::CreateSP(this, &FCustomDetailsViewDetailTreeNodeItem::GetResetToDefaultIcon);

		ResetToDefault.UIAction = FUIAction(FExecuteAction::CreateSP(this, &FCustomDetailsViewDetailTreeNodeItem::OnResetToDefaultClicked)
			, FCanExecuteAction::CreateSP(this, &FCustomDetailsViewDetailTreeNodeItem::CanResetToDefault));

		// Add Updating Reset To Default to the Slate App PostTick
		if (!UpdateResetToDefaultHandle.IsValid())
		{
			UpdateResetToDefaultHandle = FSlateApplication::Get().OnPostTick().AddSP(this, &FCustomDetailsViewDetailTreeNodeItem::UpdateResetToDefault);
		}
	}

	// Global Extensions
	if (InViewArgs.bAllowGlobalExtensions)
	{
		FPropertyEditorModule& PropertyEditorModule = FModuleManager::Get().GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		FOnGenerateGlobalRowExtensionArgs RowExtensionArgs;
		RowExtensionArgs.OwnerTreeNode  = DetailTreeNodeWeak;
		RowExtensionArgs.PropertyHandle = PropertyHandle;
		PropertyEditorModule.GetGlobalRowExtensionDelegate().Broadcast(RowExtensionArgs, ExtensionButtons);

		// Sequencer relies on getting the Keyframe Handler via the Details View of the IDetailTreeNode, but is null since there's no Details View here
		// instead add it manually here

		if (bKeyframeEnabled)
		{
			FCustomDetailsViewSequencerUtils::CreateSequencerExtensionButton(InViewArgs.KeyframeHandler, PropertyHandle, ExtensionButtons);
		}
	}

	if (ExtensionButtons.IsEmpty())
	{
		return;
	}

	FSlimHorizontalToolBarBuilder ToolbarBuilder(TSharedPtr<FUICommandList>(), FMultiBoxCustomization::None);
	ToolbarBuilder.SetLabelVisibility(EVisibility::Collapsed);
	ToolbarBuilder.SetStyle(&FAppStyle::Get(), "DetailsView.ExtensionToolBar");
	ToolbarBuilder.SetIsFocusable(false);

	for (const FPropertyRowExtensionButton& Extension : ExtensionButtons)
	{
		ToolbarBuilder.AddToolBarButton(Extension.UIAction, NAME_None, Extension.Label, Extension.ToolTip, Extension.Icon);
	}

	TSharedRef<SWidget> ExtensionWidget = SNew(SBox)
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Center)
		[
			ToolbarBuilder.MakeWidget()
		];

	Widgets.Add(ECustomDetailsViewWidgetType::Extensions, ExtensionWidget);

	InSplitter->AddSlot()
		.Value(InColumnSizeData.GetRightColumnWidth())
		.MinSize(InColumnSizeData.GetRightColumnMinWidth())
		.OnSlotResized(InColumnSizeData.GetOnRightColumnResized())
		[
			ExtensionWidget
		];
}

TSharedRef<SWidget> FCustomDetailsViewDetailTreeNodeItem::MakeEditConditionWidget()
{
	return SNew(SCheckBox)
		.OnCheckStateChanged(this, &FCustomDetailsViewDetailTreeNodeItem::OnEditConditionCheckChanged)
		.IsChecked(this, &FCustomDetailsViewDetailTreeNodeItem::GetEditConditionCheckState)
		.Visibility(this, &FCustomDetailsViewDetailTreeNodeItem::GetEditConditionVisibility);
}

bool FCustomDetailsViewDetailTreeNodeItem::HasEditConditionToggle() const
{
	return DetailWidgetRow.OnEditConditionValueChanged.IsBound();
}

EVisibility FCustomDetailsViewDetailTreeNodeItem::GetEditConditionVisibility() const
{
	return HasEditConditionToggle() ? EVisibility::Visible : EVisibility::Collapsed;
}

ECheckBoxState FCustomDetailsViewDetailTreeNodeItem::GetEditConditionCheckState() const
{
	return DetailWidgetRow.EditConditionValue.Get() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void FCustomDetailsViewDetailTreeNodeItem::OnEditConditionCheckChanged(ECheckBoxState InCheckState)
{
	checkSlow(HasEditConditionToggle());
	FScopedTransaction EditConditionChangedTransaction(LOCTEXT("EditConditionChanged", "Edit Condition Changed"));
	DetailWidgetRow.OnEditConditionValueChanged.ExecuteIfBound(InCheckState == ECheckBoxState::Checked);
}

void FCustomDetailsViewDetailTreeNodeItem::OnKeyframeClicked()
{
	const TSharedPtr<IDetailKeyframeHandler> KeyframeHandler = GetKeyframeHandler();
	if (KeyframeHandler.IsValid() && PropertyHandle.IsValid())
	{
		KeyframeHandler->OnKeyPropertyClicked(*PropertyHandle);
	}
}

bool FCustomDetailsViewDetailTreeNodeItem::IsKeyframeVisible() const
{
	const TSharedPtr<IDetailKeyframeHandler> KeyframeHandler = GetKeyframeHandler();

	if (!KeyframeHandler.IsValid() || !PropertyHandle.IsValid())
	{
		return false;
	}

	const UClass* const ObjectClass = PropertyHandle->GetOuterBaseClass();
	if (ObjectClass == nullptr)
	{
		return false;
	}

	return KeyframeHandler->IsPropertyKeyable(ObjectClass, *PropertyHandle);
}

bool FCustomDetailsViewDetailTreeNodeItem::IsResetToDefaultVisible() const
{
	return bResetToDefaultVisible;
}

void FCustomDetailsViewDetailTreeNodeItem::UpdateResetToDefault(float InDeltaTime)
{
	bResetToDefaultVisible = false;

	if (DetailWidgetRow.CustomResetToDefault.IsSet())
	{
		bResetToDefaultVisible = DetailWidgetRow.CustomResetToDefault.GetValue().IsResetToDefaultVisible(PropertyHandle);
		return;
	}

	if (PropertyHandle.IsValid())
	{
		if (PropertyHandle->HasMetaData("NoResetToDefault") || PropertyHandle->GetInstanceMetaData("NoResetToDefault"))
		{
			bResetToDefaultVisible = false;
			return;
		}
		bResetToDefaultVisible = PropertyHandle->CanResetToDefault();
	}
}

bool FCustomDetailsViewDetailTreeNodeItem::CanResetToDefault() const
{
	return IsResetToDefaultVisible() && DetailWidgetRow.ValueWidget.Widget->IsEnabled();
}

void FCustomDetailsViewDetailTreeNodeItem::OnResetToDefaultClicked()
{
	if (DetailWidgetRow.CustomResetToDefault.IsSet())
	{
		DetailWidgetRow.CustomResetToDefault.GetValue().OnResetToDefaultClicked(PropertyHandle);
	}
	else if (PropertyHandle.IsValid())
	{
		PropertyHandle->ResetToDefault();
	}
}

FText FCustomDetailsViewDetailTreeNodeItem::GetResetToDefaultToolTip() const
{
	return IsResetToDefaultVisible()
		? LOCTEXT("ResetToDefaultPropertyValueToolTip", "Reset this property to its default value.")
		: FText::GetEmpty();
}

FSlateIcon FCustomDetailsViewDetailTreeNodeItem::GetResetToDefaultIcon() const
{
	static const FSlateIcon ResetIcon_Enabled(FAppStyle::Get().GetStyleSetName(), "PropertyWindow.DiffersFromDefault");
	static const FSlateIcon ResetIcon_Disabled(FAppStyle::Get().GetStyleSetName(), "NoBrush");

	return IsResetToDefaultVisible()
		? ResetIcon_Enabled
		: ResetIcon_Disabled;
}

TSharedPtr<SWidget> FCustomDetailsViewDetailTreeNodeItem::GenerateContextMenuWidget()
{
	UToolMenus* Menus = UToolMenus::Get();

	check(Menus);

	static const FName DetailViewContextMenuName = UE::PropertyEditor::RowContextMenuName;

	if (!Menus->IsMenuRegistered(DetailViewContextMenuName))
	{
		return nullptr;
	}

	const TSharedPtr<IPropertyHandle> RowPropertyHandle = GetRowPropertyHandle();

	if (!RowPropertyHandle.IsValid())
	{
		return nullptr;
	}

	UDetailRowMenuContext* RowMenuContext = NewObject<UDetailRowMenuContext>();
	RowMenuContext->PropertyHandles.Add(RowPropertyHandle);
	RowMenuContext->DetailsView = GetDetailsView();
	RowMenuContext->ForceRefreshWidget().AddSPLambda(this, [this]
	{
		RefreshChildren();
	});

	const FToolMenuContext ToolMenuContext(RowMenuContext);
	return Menus->GenerateWidget(DetailViewContextMenuName, ToolMenuContext);
}

void FCustomDetailsViewDetailTreeNodeItem::GenerateCustomChildren(const TSharedRef<ICustomDetailsViewItem>& InParentItem, TArray<TSharedPtr<ICustomDetailsViewItem>>& OutChildren)
{
	if (!CustomDetailsViewWeak.IsValid())
	{
		return;
	}

	const TSharedPtr<IDetailTreeNode> DetailTreeNode = DetailTreeNodeWeak.Pin();

	if (!DetailTreeNode.IsValid())
	{
		return;
	}

	const ECustomDetailsViewNodePropertyFlag ChildNodePropertyFlags = (IsStruct() || HasParentStruct())
		? ECustomDetailsViewNodePropertyFlag::HasParentStruct
		: ECustomDetailsViewNodePropertyFlag::None;

	TArray<TSharedRef<IDetailTreeNode>> NodeChildren;
	DetailTreeNode->GetChildren(NodeChildren);

	AddChildDetailsTreeNodes(InParentItem, ChildNodePropertyFlags, NodeChildren, OutChildren);
}

void FCustomDetailsViewDetailTreeNodeItem::AddChildDetailsTreeNodes(const TSharedRef<ICustomDetailsViewItem>& InParentItem, ECustomDetailsViewNodePropertyFlag InNodeChildPropertyFlag,
	const TArray<TSharedRef<IDetailTreeNode>>& InNodeChildren, TArray<TSharedPtr<ICustomDetailsViewItem>>& OutChildren)
{
	if (!CustomDetailsViewWeak.IsValid())
	{
		return;
	}

	const TSharedRef<SCustomDetailsView> CustomDetailsView = CustomDetailsViewWeak.Pin().ToSharedRef();

	for (const TSharedRef<IDetailTreeNode>& ChildTreeNode : InNodeChildren)
	{
		using namespace UE::CustomDetailsView::Private;
		const EAllowType AllowType = CustomDetailsView->GetAllowType(ChildTreeNode, InNodeChildPropertyFlag);

		// If DisallowSelfAndChildren, this Tree Node Path is completely blocked, continue.
		if (AllowType == EAllowType::DisallowSelfAndChildren)
		{
			continue;
		}

		// If DisallowSelf, grab the children nodes. Self's Children node's parent is set to Self's Parent rather than Self
		if (AllowType == EAllowType::DisallowSelf)
		{
			FCustomDetailsViewDetailTreeNodeItem ChildItem = FCustomDetailsViewDetailTreeNodeItem(CustomDetailsView, InParentItem, ChildTreeNode);
			ChildItem.RefreshItemId();
			ChildItem.RefreshChildren(InParentItem);

			OutChildren.Append(ChildItem.GetChildren());

			continue;
		}

		// Support Type here has to be allowed
		check(AllowType == EAllowType::Allowed);

		TSharedRef<FCustomDetailsViewDetailTreeNodeItem> Item = CustomDetailsView->CreateItem<FCustomDetailsViewDetailTreeNodeItem>(CustomDetailsView
			, InParentItem, ChildTreeNode);

		Item->AddAsChild(InParentItem, OutChildren);
	}
}

bool FCustomDetailsViewDetailTreeNodeItem::IsStruct() const
{
	if (PropertyHandle.IsValid())
	{
		if (FProperty* Property = PropertyHandle->GetProperty())
		{
			return Property->IsA<FStructProperty>();
		}
	}

	return false;
}

bool FCustomDetailsViewDetailTreeNodeItem::HasParentStruct() const
{
	for (TSharedPtr<ICustomDetailsViewItem> Parent = GetParent(); Parent.IsValid(); Parent = Parent->GetParent())
	{
		if (Parent->GetItemId().IsType(EDetailNodeType::Item))
		{
			if (StaticCastSharedPtr<FCustomDetailsViewDetailTreeNodeItem>(Parent)->IsStruct())
			{
				return true;
			}
		}
	}

	return false;
}


#undef LOCTEXT_NAMESPACE
