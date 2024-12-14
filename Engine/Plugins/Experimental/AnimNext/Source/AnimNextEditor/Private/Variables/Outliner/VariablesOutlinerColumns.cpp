// Copyright Epic Games, Inc. All Rights Reserved.

#include "VariablesOutlinerColumns.h"

#include "AnimNextEditorModule.h"
#include "AnimNextRigVMAssetEditorData.h"
#include "DetailLayoutBuilder.h"
#include "EditorUtils.h"
#include "IAnimNextRigVMExportInterface.h"
#include "Variables/IAnimNextRigVMVariableInterface.h"
#include "InstancedPropertyBagStructureDataProvider.h"
#include "ISinglePropertyView.h"
#include "PropertyBagDetails.h"
#include "ScopedTransaction.h"
#include "SPinTypeSelector.h"
#include "UncookedOnlyUtils.h"
#include "VariablesOutlinerEntryItem.h"
#include "Entries/AnimNextDataInterfaceEntry.h"
#include "Entries/AnimNextRigVMAssetEntry.h"
#include "Entries/AnimNextVariableEntry.h"
#include "Widgets/Images/SImage.h"
#include "UObject/Package.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Variables/SVariableOverride.h"

#define LOCTEXT_NAMESPACE "VariablesOutlinerColumns"

namespace UE::AnimNext::Editor
{

FLazyName VariablesOutlinerType("Type");

FName FVariablesOutlinerTypeColumn::GetID()
{
	return VariablesOutlinerType;
}

SHeaderRow::FColumn::FArguments FVariablesOutlinerTypeColumn::ConstructHeaderRowColumn()
{
	return SHeaderRow::Column(GetColumnID())
		.FixedWidth(24.f)
		.HAlignHeader(HAlign_Center)
		.VAlignHeader(VAlign_Center)
		.HAlignCell(HAlign_Center)
		.VAlignCell(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(16.0f)
			.HeightOverride(16.0f)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Center)
			[
				SNew(SImage)
				.ColorAndOpacity(FSlateColor::UseForeground())
				.Image(FAppStyle::GetBrush(TEXT("Kismet.VariableList.TypeIcon")))
				.ToolTipText(LOCTEXT("TypeTooltip", "Type of this entry"))
			]
		];
}

const TSharedRef<SWidget> FVariablesOutlinerTypeColumn::ConstructRowWidget(FSceneOutlinerTreeItemRef Item, const STableRow<FSceneOutlinerTreeItemPtr>& Row)
{
	const FVariablesOutlinerEntryItem* TreeItem = Item->CastTo<FVariablesOutlinerEntryItem>();
	if (TreeItem == nullptr)
	{
		return SNullWidget::NullWidget;
	}

	return
		SNew(SBox)
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		.IsEnabled(!TreeItem->WeakDataInterfaceEntry.IsValid())
		[
			SNew(SPinTypeSelector, FGetPinTypeTree::CreateStatic(&Editor::FUtils::GetFilteredVariableTypeTree))
			.TargetPinType_Lambda([WeakEntry = TreeItem->WeakEntry]()
			{
				if(const IAnimNextRigVMVariableInterface* Variable = Cast<IAnimNextRigVMVariableInterface>(WeakEntry.Get()))
				{
					return UncookedOnly::FUtils::GetPinTypeFromParamType(Variable->GetType());
				}

				return FEdGraphPinType();
			})
			.OnPinTypeChanged_Lambda([WeakEntry = TreeItem->WeakEntry](const FEdGraphPinType& PinType)
			{
				if(IAnimNextRigVMVariableInterface* Variable = Cast<IAnimNextRigVMVariableInterface>(WeakEntry.Get()))
				{
					const FAnimNextParamType ParamType = UncookedOnly::FUtils::GetParamTypeFromPinType(PinType);
					if(ParamType.IsValid())
					{
						FScopedTransaction Transaction(LOCTEXT("SetTypeTransaction", "Set Variable Type"));
						Variable->SetType(ParamType);
					}
				}
			})
			.Schema(GetDefault<UPropertyBagSchema>())
			.bAllowArrays(true)
			.TypeTreeFilter(ETypeTreeFilter::None)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.SelectorType(SPinTypeSelector::ESelectorType::Compact)
		];
}

FLazyName VariablesOutlinerValue("Value");

FName FVariablesOutlinerValueColumn::GetID()
{
	return VariablesOutlinerValue;
}

SHeaderRow::FColumn::FArguments FVariablesOutlinerValueColumn::ConstructHeaderRowColumn()
{
	return
		SHeaderRow::Column(GetColumnID())
		.FillWidth(1.0f)
		.HAlignHeader(HAlign_Left)
		.VAlignHeader(VAlign_Center)
		.HAlignCell(HAlign_Fill)
		.VAlignCell(VAlign_Center)
		[
			SNew(SBox) 
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Fill)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ValueLabel", "Value"))
				.ToolTipText(LOCTEXT("ValueTooltip", "Value of the variable"))
			]
		];
}

class SVariablesOutlinerValue : public SCompoundWidget, public FNotifyHook
{
	SLATE_BEGIN_ARGS(SVariablesOutlinerValue) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, FVariablesOutlinerEntryItem& InTreeItem, ISceneOutliner& SceneOutliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow)
	{
		WeakTreeItem = StaticCastSharedRef<FVariablesOutlinerEntryItem>(InTreeItem.AsShared());

		if(UAnimNextVariableEntry* VariableEntry = Cast<UAnimNextVariableEntry>(InTreeItem.WeakEntry.Get()))
		{
			if(UAnimNextRigVMAssetEditorData* EditorData = VariableEntry->GetTypedOuter<UAnimNextRigVMAssetEditorData>())
			{
				EditorData->ModifiedDelegate.AddSP(this, &SVariablesOutlinerValue::HandleModified);
			}
		}
		if(UAnimNextDataInterfaceEntry* DataInterfaceEntry = Cast<UAnimNextDataInterfaceEntry>(InTreeItem.WeakDataInterfaceEntry.Get()))
		{
			if(UAnimNextRigVMAssetEditorData* EditorData = DataInterfaceEntry->GetTypedOuter<UAnimNextRigVMAssetEditorData>())
			{
				EditorData->ModifiedDelegate.AddSP(this, &SVariablesOutlinerValue::HandleModified);
			}
		}

		ChildSlot
		[
			SAssignNew(WidgetContainer, SBox)
		];

		BuildValueWidget();
	}

	void BuildValueWidget()
	{
		TSharedPtr<FVariablesOutlinerEntryItem> TreeItem = WeakTreeItem.Pin();
		if(!TreeItem.IsValid())
		{
			return;
		}

		UAnimNextVariableEntry* VariableEntry = TreeItem->WeakEntry.Get();
		if(VariableEntry == nullptr)
		{
			return;
		}

		TSharedPtr<SWidget> ValueWidget = SNullWidget::NullWidget;
		const FName VariableName = VariableEntry->GetEntryName();
		FInstancedPropertyBag* PropertyBag = nullptr;
		FName ValueName;
		bool bCanOverride = false;
		UAnimNextDataInterfaceEntry* DataInterfaceEntry = TreeItem->WeakDataInterfaceEntry.Get();
		if(DataInterfaceEntry != nullptr)
		{
			// Update for override status
			bCanOverride = true;
			OverrideStatus = DataInterfaceEntry->FindValueOverridePropertyBagRecursive(VariableName, PropertyBag);
			switch(OverrideStatus)
			{
			case EAnimNextDataInterfaceValueOverrideStatus::NotOverridden:
				{
					// Use the internal property bag (copying from the source) and the default name
					InternalPropertyBag = VariableEntry->GetPropertyBag();
					PropertyBag = &InternalPropertyBag;
					ValueName = IAnimNextRigVMVariableInterface::ValueName;
					break;
				}
			case EAnimNextDataInterfaceValueOverrideStatus::OverriddenInThisAsset:
				{
					// Use the found property bag and the variable name
					check(PropertyBag);
					ValueName = VariableName;
					break;
				}
			case EAnimNextDataInterfaceValueOverrideStatus::OverriddenInParentAsset:
				{
					// Use the internal property bag (copying just the value from the overriding asset's property bag) and the default name
					check(PropertyBag);
					const FPropertyBagPropertyDesc* Desc = PropertyBag->FindPropertyDescByName(VariableName);
					check(Desc);
					const uint8* DataPtr = Desc->CachedProperty->ContainerPtrToValuePtr<uint8>(PropertyBag->GetValue().GetMemory());
					TConstArrayView<uint8> Value(DataPtr, Desc->CachedProperty->GetElementSize());
					InternalPropertyBag.ReplaceAllPropertiesAndValues(TConstArrayView<FPropertyBagPropertyDesc>(Desc, 1), TConstArrayView<TConstArrayView<uint8>>(&Value, 1));
					PropertyBag = &InternalPropertyBag;
					ValueName = VariableName;
					break;
				}
			}
		}
		else
		{
			// No data interface present, use the variable's internal property bag
			bCanOverride = false;
			PropertyBag = &VariableEntry->GetMutablePropertyBag();
			ValueName = IAnimNextRigVMVariableInterface::ValueName;
		}

		const FPropertyBagPropertyDesc* PropertyDesc = PropertyBag ? PropertyBag->FindPropertyDescByName(ValueName) : nullptr;
		if (PropertyDesc != nullptr)
		{
			if (PropertyDesc->ContainerTypes.IsEmpty()) // avoid trying to inline containers
			{
				FSinglePropertyParams SinglePropertyArgs;
				SinglePropertyArgs.NamePlacement = EPropertyNamePlacement::Hidden;
				SinglePropertyArgs.NotifyHook = this;
				SinglePropertyArgs.bHideResetToDefault = true;

				FPropertyEditorModule& PropertyEditorModule = FModuleManager::Get().LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
				
				const TSharedPtr<ISinglePropertyView> SingleStructPropertyView = PropertyEditorModule.CreateSingleProperty(MakeShared<FInstancePropertyBagStructureDataProvider>(*PropertyBag), ValueName, SinglePropertyArgs);
				if (SingleStructPropertyView.IsValid())
				{
					ValueWidget = SNew(SHorizontalBox)
						+SHorizontalBox::Slot()
						.FillWidth(1.0f)
						[
							SNew(SBox)
							.HAlign(HAlign_Left)
							[
								SingleStructPropertyView.ToSharedRef()
							]
						]
						+SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.HAlign(HAlign_Center)
						[
							SNew(SVariableOverride, DataInterfaceEntry, VariableName)
							.Visibility(bCanOverride ? EVisibility::Visible : EVisibility::Collapsed)
							.OverrideStatus(OverrideStatus)
						];
				}
			}
		}

		WidgetContainer->SetContent(ValueWidget.ToSharedRef());
	}

	// FNotifyHook interface
	virtual void NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged) override
	{
		TSharedPtr<FVariablesOutlinerEntryItem> TreeItem = WeakTreeItem.Pin();
		if(!TreeItem.IsValid())
		{
			return;
		}

		UAnimNextVariableEntry* VariableEntry = Cast<UAnimNextVariableEntry>(TreeItem->WeakEntry.Get());
		if(VariableEntry == nullptr)
		{
			return;
		}

		UAnimNextRigVMAssetEditorData* EditorData = VariableEntry->GetTypedOuter<UAnimNextRigVMAssetEditorData>();
		if (EditorData == nullptr)
		{
			return;
		}

		UAnimNextDataInterfaceEntry* DataInterfaceEntry = Cast<UAnimNextDataInterfaceEntry>(TreeItem->WeakDataInterfaceEntry.Get());
		if(DataInterfaceEntry != nullptr)
		{
			const FName VariableName = VariableEntry->GetVariableName();
			if(!TreeItem->WeakDataInterfaceEntry->HasValueOverride(VariableName))
			{
				// No value override yet, so we copy from the internal property bag
				check(InternalPropertyBag.IsValid() && InternalPropertyBag.GetPropertyBagStruct()->GetPropertyDescs().Num() == 1);
				const FProperty* Property = InternalPropertyBag.GetPropertyBagStruct()->GetPropertyDescs()[0].CachedProperty; 
				const uint8* DataPtr = Property->ContainerPtrToValuePtr<uint8>(InternalPropertyBag.GetValue().GetMemory());
				TreeItem->WeakDataInterfaceEntry->SetValueOverride(VariableName, VariableEntry->GetType(), TConstArrayView<uint8>(DataPtr, Property->GetElementSize()));
				ensure(TreeItem->WeakDataInterfaceEntry->HasValueOverride(VariableName));
			}

			DataInterfaceEntry->MarkPackageDirty();
			DataInterfaceEntry->BroadcastModified(EAnimNextEditorDataNotifType::VariableDefaultValueChanged);
		}
		else
		{
			VariableEntry->MarkPackageDirty();
			VariableEntry->BroadcastModified(EAnimNextEditorDataNotifType::VariableDefaultValueChanged);
		}
	}

	void HandleModified(UAnimNextRigVMAssetEditorData* InEditorData, EAnimNextEditorDataNotifType InType, UObject* InSubject)
	{
		if( InType != EAnimNextEditorDataNotifType::VariableDefaultValueChanged &&
			InType != EAnimNextEditorDataNotifType::UndoRedo)
		{
			return;
		}
		
		TSharedPtr<FVariablesOutlinerEntryItem> TreeItem = WeakTreeItem.Pin();
		if(!TreeItem.IsValid())
		{
			return;
		}

		UAnimNextVariableEntry* VariableEntry = Cast<UAnimNextVariableEntry>(TreeItem->WeakEntry.Get());
		UAnimNextDataInterfaceEntry* DataInterfaceEntry = Cast<UAnimNextDataInterfaceEntry>(TreeItem->WeakDataInterfaceEntry.Get());
		if(VariableEntry != InSubject && DataInterfaceEntry != InSubject)
		{
			return;
		}

		BuildValueWidget();
	}

	TWeakPtr<FVariablesOutlinerEntryItem> WeakTreeItem;
	TSharedPtr<SBox> WidgetContainer;
	FInstancedPropertyBag InternalPropertyBag;
	EAnimNextDataInterfaceValueOverrideStatus OverrideStatus = EAnimNextDataInterfaceValueOverrideStatus::NotOverridden;
};

const TSharedRef<SWidget> FVariablesOutlinerValueColumn::ConstructRowWidget(FSceneOutlinerTreeItemRef Item, const STableRow<FSceneOutlinerTreeItemPtr>& Row)
{
	FVariablesOutlinerEntryItem* TreeItem = Item->CastTo<FVariablesOutlinerEntryItem>();
	if (TreeItem == nullptr)
	{
		return SNullWidget::NullWidget;
	}

	IAnimNextRigVMVariableInterface* Variable = Cast<IAnimNextRigVMVariableInterface>(TreeItem->WeakEntry.Get());
	if(Variable == nullptr)
	{
		return SNullWidget::NullWidget;
	}
	
	return SNew(SVariablesOutlinerValue, *TreeItem, WeakSceneOutliner.Pin().ToSharedRef().Get(), Row);
}

FLazyName VariablesOutlinerAccessSpecifier("AccessSpecifier");

FName FVariablesOutlinerAccessSpecifierColumn::GetID()
{
	return VariablesOutlinerAccessSpecifier;
}

SHeaderRow::FColumn::FArguments FVariablesOutlinerAccessSpecifierColumn::ConstructHeaderRowColumn()
{
	return
		SHeaderRow::Column(GetColumnID())
		.FixedWidth(24.0f)
		.HAlignHeader(HAlign_Center)
		.VAlignHeader(VAlign_Center)
		.HAlignCell(HAlign_Fill)
		.VAlignCell(VAlign_Fill)
		[
			SNew(SBox) 
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Center)
			[
				SNew(SImage)
				.ColorAndOpacity(FSlateColor::UseForeground())
				.Image(FAppStyle::GetBrush("Level.VisibleIcon16x"))
				.ToolTipText(LOCTEXT("AccessSpecifierAccessLevelTooltip", "Access level of this entry"))
			]
		];
}

class SVariablesOutlinerAccessSpecifier : public SCompoundWidget, public FNotifyHook
{
	SLATE_BEGIN_ARGS(SVariablesOutlinerAccessSpecifier) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, FVariablesOutlinerEntryItem& InTreeItem, ISceneOutliner& SceneOutliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow)
	{
		WeakTreeItem = StaticCastSharedRef<FVariablesOutlinerEntryItem>(InTreeItem.AsShared());

		ChildSlot
		[
			SNew(SBox)
			.IsEnabled(!InTreeItem.WeakDataInterfaceEntry.IsValid())
			.WidthOverride(16.0f)
			.HeightOverride(16.0f)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "HoverHintOnly")
				.OnClicked(this, &SVariablesOutlinerAccessSpecifier::OnClicked)
				.Content()
				[
					SNew(SImage)
					.ColorAndOpacity(FSlateColor::UseForeground())
					.Image(this, &SVariablesOutlinerAccessSpecifier::GetImage)
					.ToolTipText(this, &SVariablesOutlinerAccessSpecifier::GetTooltipText)
				]
			]
		];
	}

	FReply OnClicked()
	{
		TSharedPtr<FVariablesOutlinerEntryItem> TreeItem = WeakTreeItem.Pin();
		if(!TreeItem.IsValid())
		{
			return FReply::Unhandled();
		}
		
		IAnimNextRigVMExportInterface* Export = Cast<IAnimNextRigVMExportInterface>(TreeItem->WeakEntry.Get());
		if(Export == nullptr)
		{
			return FReply::Unhandled();
		}

		FScopedTransaction Transaction(LOCTEXT("SetAccessSpecifierTransaction", "Set Access Specifier"));
		Export->SetExportAccessSpecifier(Export->GetExportAccessSpecifier() == EAnimNextExportAccessSpecifier::Public ? EAnimNextExportAccessSpecifier::Private : EAnimNextExportAccessSpecifier::Public);  

		return FReply::Unhandled();	// Fall through so we dont deselect our item
	}

	const FSlateBrush* GetImage() const
	{
		TSharedPtr<FVariablesOutlinerEntryItem> TreeItem = WeakTreeItem.Pin();
		if(!TreeItem.IsValid())
		{
			return nullptr;
		}

		IAnimNextRigVMExportInterface* Export = Cast<IAnimNextRigVMExportInterface>(TreeItem->WeakEntry.Get());
		if(Export == nullptr)
		{
			return nullptr;
		}

		return Export->GetExportAccessSpecifier() == EAnimNextExportAccessSpecifier::Public ?
			FAppStyle::GetBrush("Level.VisibleIcon16x") :
			FAppStyle::GetBrush("Level.NotVisibleHighlightIcon16x");
	}

	FText GetTooltipText() const
	{
		TSharedPtr<FVariablesOutlinerEntryItem> TreeItem = WeakTreeItem.Pin();
		if(!TreeItem.IsValid())
		{
			return FText::GetEmpty();
		}

		IAnimNextRigVMExportInterface* Export = Cast<IAnimNextRigVMExportInterface>(TreeItem->WeakEntry.Get());
		if(Export == nullptr)
		{
			return FText::GetEmpty();
		}

		FText AccessSpecifier = Export->GetExportAccessSpecifier() == EAnimNextExportAccessSpecifier::Public ? LOCTEXT("PublicSpecifier", "public") : LOCTEXT("PrivateSpecifier", "private");
		FText AccessSpecifierDesc = Export->GetExportAccessSpecifier() == EAnimNextExportAccessSpecifier::Public ?
			LOCTEXT("PublicSpecifierDesc", "This means that the entry is usable from gameplay and from other AnimNext assets") :
			LOCTEXT("PrivateSpecifierDesc", "This means that the entry is only usable inside this asset");
		return FText::Format(LOCTEXT("AccessSpecifierEntryTooltip", "This entry is {0}.\n{1}"), AccessSpecifier, AccessSpecifierDesc);
	}

	TWeakPtr<FVariablesOutlinerEntryItem> WeakTreeItem;
};

const TSharedRef<SWidget> FVariablesOutlinerAccessSpecifierColumn::ConstructRowWidget(FSceneOutlinerTreeItemRef Item, const STableRow<FSceneOutlinerTreeItemPtr>& Row)
{
	FVariablesOutlinerEntryItem* TreeItem = Item->CastTo<FVariablesOutlinerEntryItem>();
	if (TreeItem == nullptr)
	{
		return SNullWidget::NullWidget;
	}

	IAnimNextRigVMVariableInterface* Variable = Cast<IAnimNextRigVMVariableInterface>(TreeItem->WeakEntry.Get());
	if(Variable == nullptr)
	{
		return SNullWidget::NullWidget;
	}
	
	return SNew(SVariablesOutlinerAccessSpecifier, *TreeItem, WeakSceneOutliner.Pin().ToSharedRef().Get(), Row);
}

}

#undef LOCTEXT_NAMESPACE // "VariablesOutlinerColumns"