// Copyright Epic Games, Inc. All Rights Reserved.

#include "SAvaRundownRCPropertyItemRow.h"
#include "AvaRundownPageRemoteControlWidgetUtils.h"
#include "AvaRundownRCPropertyItem.h"
#include "GameFramework/Actor.h"
#include "IDetailTreeNode.h"
#include "Internationalization/Text.h"
#include "IPropertyRowGenerator.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "RemoteControlEntity.h"
#include "RemoteControlField.h"
#include "RemoteControlPreset.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SAvaRundownRCPropertyItemRow"

void SAvaRundownRCPropertyItemRow::Construct(const FArguments& InArgs, TSharedRef<SAvaRundownPageRemoteControlProps> InPropertyPanel,
	const TSharedRef<STableViewBase>& InOwnerTableView, const TSharedPtr<const FAvaRundownRCPropertyItem>& InRowItem)
{
	ItemPtrWeak = InRowItem;
	PropertyPanelWeak = InPropertyPanel;
	NotifyHook = InPropertyPanel->GetNotifyHook();
	Generator = nullptr;
	ValueContainer = nullptr;
	ValueWidget = nullptr;

	SMultiColumnTableRow<FAvaRundownRCPropertyItemPtr>::Construct(FSuperRowType::FArguments(), InOwnerTableView);
}

TSharedRef<SWidget> SAvaRundownRCPropertyItemRow::GenerateWidgetForColumn(const FName& InColumnName)
{
	TSharedPtr<const FAvaRundownRCPropertyItem> ItemPtr = ItemPtrWeak.Pin();

	if (ItemPtr.IsValid())
	{
		if (InColumnName == SAvaRundownPageRemoteControlProps::PropertyColumnName)
		{
			return SNew(STextBlock)
				.Margin(FMargin(8.f, 2.f, 0.f, 2.f))
				.Text(GetFieldLabel())
				.ToolTipText(this, &SAvaRundownRCPropertyItemRow::GetPropertyTooltipText);
		}
		else if (InColumnName == SAvaRundownPageRemoteControlProps::ValueColumnName)
		{
			return SAssignNew(ValueContainer, SBox)
				[
					CreateValue()
				];
		}
		else
		{
			TSharedPtr<SAvaRundownPageRemoteControlProps> PropertyPanel = PropertyPanelWeak.Pin();

			if (PropertyPanel.IsValid())
			{
				TSharedPtr<SWidget> Cell = nullptr;
				const TArray<FAvaRundownRCPropertyTableRowExtensionDelegate>& TableRowExtensionDelegates = PropertyPanel->GetTableRowExtensionDelegates(InColumnName);

				for (const FAvaRundownRCPropertyTableRowExtensionDelegate& TableRowExtensionDelegate : TableRowExtensionDelegates)
				{
					TableRowExtensionDelegate.ExecuteIfBound(PropertyPanel.ToSharedRef(), ItemPtr.ToSharedRef(), Cell);
				}

				if (Cell.IsValid())
				{
					return Cell.ToSharedRef();
				}
			}
		}
	}

	return SNullWidget::NullWidget;
}

void SAvaRundownRCPropertyItemRow::UpdateValue()
{
	if (ValueContainer.IsValid())
	{
		ValueContainer->SetContent(CreateValue());
	}
}

FText SAvaRundownRCPropertyItemRow::GetFieldLabel() const
{
	if (TSharedPtr<const FAvaRundownRCPropertyItem> ItemPtr = ItemPtrWeak.Pin())
	{
		if (TSharedPtr<FRemoteControlEntity> EntityPtr = ItemPtr->GetEntity())
		{
			TSharedPtr<FRemoteControlField> FieldPtr = StaticCastSharedPtr<FRemoteControlField>(EntityPtr);
			return FText::FromName(FieldPtr->FieldName);
		}
	}

	return FText::GetEmpty();
}

TSharedRef<SWidget> SAvaRundownRCPropertyItemRow::CreateValue()
{
	if (TSharedPtr<const FAvaRundownRCPropertyItem> ItemPtr = ItemPtrWeak.Pin())
	{
		if (TSharedPtr<FRemoteControlEntity> EntityPtr = ItemPtr->GetEntity())
		{
			TSharedPtr<FRemoteControlField> FieldPtr = StaticCastSharedPtr<FRemoteControlField>(EntityPtr);

			// For the moment, just use the first object.
			TArray<UObject*> Objects = FieldPtr->GetBoundObjects();

			if ((FieldPtr->FieldType == EExposedFieldType::Property) && (Objects.Num() > 0))
			{
				FPropertyRowGeneratorArgs Args;
				Args.NotifyHook = NotifyHook.Get();
				Generator = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor").CreatePropertyRowGenerator(Args);
				Generator->SetObjects({Objects[0]});

				if (TSharedPtr<IDetailTreeNode> Node = FAvaRundownPageRemoteControlWidgetUtils::FindNode(Generator->GetRootTreeNodes(), FieldPtr->FieldPathInfo.ToPathPropertyString(), FAvaRundownPageRemoteControlWidgetUtils::EFindNodeMethod::Path))
				{
					const FNodeWidgets NodeWidgets = Node->CreateNodeWidgets();
					ValueWidget = NodeWidgets.WholeRowWidget.IsValid() ? NodeWidgets.WholeRowWidget : NodeWidgets.ValueWidget;

					if (ItemPtr->IsEntityControlled())
					{
						ValueWidget = SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.VAlign(EVerticalAlignment::VAlign_Center)
							[
								ValueWidget.ToSharedRef()
							]
							+ SHorizontalBox::Slot()
							.VAlign(EVerticalAlignment::VAlign_Center)
							.Padding(3.f, 0.f, 0.f, 0.f)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("Controlled", "(Controlled)"))
							];

						ValueWidget->SetEnabled(false);
					}

					return ValueWidget.ToSharedRef();
				}
			}
		}
	}

	return SNullWidget::NullWidget;
}

FText SAvaRundownRCPropertyItemRow::GetPropertyTooltipText() const
{
	TSharedPtr<const FAvaRundownRCPropertyItem> ItemPtr = ItemPtrWeak.Pin();

	FText OwnerText = LOCTEXT("InvalidOwnerText", "(Invalid)");
	FText SubobjectPathText = LOCTEXT("InvalidSubobjectPathText", "(Invalid)");

	const TSharedPtr<FRemoteControlEntity> Entity = ItemPtr->GetEntity();
	if (Entity.IsValid())
	{
		const FString BindingPath = Entity->GetLastBindingPath().ToString();

		FName OwnerName;
		if (UObject* Object = Entity->GetBoundObject())
		{
			if (AActor* OwnerActor = Object->GetTypedOuter<AActor>())
			{
				OwnerText = FText::FromString(OwnerActor->GetActorLabel());
				OwnerName = OwnerActor->GetFName();
			}
			else if (AActor* Actor = Cast<AActor>(Object))
			{
				OwnerText = FText::FromString(Actor->GetActorLabel());
				OwnerName = Object->GetFName();
			}
			else
			{
				OwnerText = FText::FromString(Object->GetName());
				OwnerName = Object->GetFName();
			}
		}
		else
		{
			static const FString PersistentLevelString = TEXT(":PersistentLevel.");
			const int32 PersistentLevelIndex = BindingPath.Find(PersistentLevelString);
			if (PersistentLevelIndex != INDEX_NONE)
			{
				OwnerText = FText::FromName(OwnerName);
				OwnerName = *BindingPath.RightChop(PersistentLevelIndex + PersistentLevelString.Len());
			}
		}

		const int32 OwnerNameIndex = BindingPath.Find(OwnerName.ToString() + TEXT("."));
		if (OwnerNameIndex != INDEX_NONE)
		{
			SubobjectPathText = FText::FromString(*BindingPath.RightChop(OwnerNameIndex + OwnerName.GetStringLength() + 1));
		}
	}

	return FText::Format(LOCTEXT("PropertyTooltipText", "Owner: {0}\nSubobjectPath: {1}"), OwnerText, SubobjectPathText);
}

#undef LOCTEXT_NAMESPACE
