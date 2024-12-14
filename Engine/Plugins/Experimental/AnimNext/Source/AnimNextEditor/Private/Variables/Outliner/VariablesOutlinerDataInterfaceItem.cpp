// Copyright Epic Games, Inc. All Rights Reserved.

#include "VariablesOutlinerDataInterfaceItem.h"

#include "ISceneOutliner.h"
#include "Styling/SlateColor.h"
#include "Misc/Attribute.h"
#include "Misc/Optional.h"
#include "SceneOutlinerStandaloneTypes.h"
#include "AnimNextEditorModule.h"
#include "AnimNextRigVMAssetEditorData.h"
#include "DataInterface/AnimNextDataInterface.h"
#include "Entries/AnimNextDataInterfaceEntry.h"
#include "Styling/SlateIconFinder.h"
#include "Styling/StarshipCoreStyle.h"
#include "UObject/Package.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/SListView.h"

#define LOCTEXT_NAMESPACE "VariablesOutlinerTreeItem"

namespace UE::AnimNext::Editor
{

const FSceneOutlinerTreeItemType FVariablesOutlinerDataInterfaceItem::Type;

class SVariablesOutlinerDataInterfaceLabel : FSceneOutlinerCommonLabelData, public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SVariablesOutlinerDataInterfaceLabel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, FVariablesOutlinerDataInterfaceItem& InTreeItem, ISceneOutliner& SceneOutliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow)
	{
		WeakSceneOutliner = StaticCastSharedRef<ISceneOutliner>(SceneOutliner.AsShared());
		TreeItem = StaticCastSharedRef<FVariablesOutlinerDataInterfaceItem>(InTreeItem.AsShared());

		FText AssetName;
		if(UAnimNextDataInterfaceEntry* AssetEntry = InTreeItem.WeakEntry.Get())
		{
			AssetName = FText::FromString(AssetEntry->GetDataInterfacePath().ToString());
		}
		else
		{
			AssetName = LOCTEXT("UnknownAssetName", "Unknown Asset");
		}
		
		ChildSlot
		[
			SNew(SHorizontalBox)
			.ToolTipText(FText::Format(LOCTEXT("ImportedVariablesFormat", "Imported variables from '{0}'"), AssetName))
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(2.0f, 2.0f)
			[
				SNew(SImage)
				.Image(FAppStyle::Get().GetBrush("Icons.Import"))
			]
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 2.0f)
			[
				SNew(STextBlock)
				.Font(FStyleFonts::Get().NormalBold)
				.Text(this, &SVariablesOutlinerDataInterfaceLabel::GetDisplayText)
				.HighlightText(SceneOutliner.GetFilterHighlightText())
				.ColorAndOpacity(this, &SVariablesOutlinerDataInterfaceLabel::GetForegroundColor)
			]
			+SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.Padding(2.0f, 0.0f, 2.0f, 3.0f)
			.AutoWidth()
			[
				SNew(SImage)
				.Visibility(this, &SVariablesOutlinerDataInterfaceLabel::GetDirtyImageVisibility)
				.ToolTipText(this, &SVariablesOutlinerDataInterfaceLabel::GetDirtyTooltipText)
				.Image(this, &SVariablesOutlinerDataInterfaceLabel::GetDirtyImageBrush)
			]
		];
	}

	FText GetDirtyTooltipText() const
	{
		if (const TSharedPtr<FVariablesOutlinerDataInterfaceItem> Item = TreeItem.Pin())
		{
			FTextBuilder TextBuilder;
			TextBuilder.AppendLine(LOCTEXT("ModifiedTooltip", "Modified"));

			if(UAnimNextDataInterfaceEntry* AssetEntry = Item->WeakEntry.Get())
			{
				const UPackage* ExternalPackage = AssetEntry->GetExternalPackage();
				if(ExternalPackage && ExternalPackage->IsDirty())
				{
					TextBuilder.AppendLine(FText::FromName(ExternalPackage->GetFName()));
				}
			}

			return TextBuilder.ToText();
		}
		return FText::GetEmpty();
	}

	const FSlateBrush* GetDirtyImageBrush() const
	{
		if (const TSharedPtr<FVariablesOutlinerDataInterfaceItem> Item = TreeItem.Pin())
		{
			if(UAnimNextDataInterfaceEntry* AssetEntry = Item->WeakEntry.Get())
			{
				bool bIsDirty = false;
				const UPackage* ExternalPackage = AssetEntry->GetExternalPackage();
				if(ExternalPackage && ExternalPackage->IsDirty())
				{
					bIsDirty = true;
				}

				return bIsDirty ? FAppStyle::GetBrush("Icons.DirtyBadge") : nullptr;
			}
		}
		return nullptr;
	}

	EVisibility GetDirtyImageVisibility() const
	{
		if (const TSharedPtr<FVariablesOutlinerDataInterfaceItem> Item = TreeItem.Pin())
		{
			if(UAnimNextDataInterfaceEntry* AssetEntry = Item->WeakEntry.Get())
			{
				bool bIsDirty = false;
				const UPackage* Package = AssetEntry->GetPackage();
				check(Package);
				if(Package->IsDirty())
				{
					bIsDirty = true;
				}

				return bIsDirty ? EVisibility::Visible : EVisibility::Collapsed;
			}
		}
		return EVisibility::Collapsed;
	}
	
	FText GetDisplayText() const
	{
		if (const TSharedPtr<FVariablesOutlinerDataInterfaceItem> Item = TreeItem.Pin())
		{
			return FText::FromString(Item->GetDisplayString());
		}
		return FText();
	}

	virtual FSlateColor GetForegroundColor() const override
	{
		const TOptional<FLinearColor> BaseColor = FSceneOutlinerCommonLabelData::GetForegroundColor(*TreeItem.Pin());
		return BaseColor.IsSet() ? BaseColor.GetValue() : FSlateColor::UseForeground();
	}

	TWeakPtr<FVariablesOutlinerDataInterfaceItem> TreeItem;
	TSharedPtr<SInlineEditableTextBlock> TextBlock;
};

FVariablesOutlinerDataInterfaceItem::FVariablesOutlinerDataInterfaceItem(UAnimNextDataInterfaceEntry* InEntry)
	: ISceneOutlinerTreeItem(FVariablesOutlinerDataInterfaceItem::Type)
	, WeakEntry(InEntry)
{
}

bool FVariablesOutlinerDataInterfaceItem::IsValid() const
{
	return WeakEntry.Get() != nullptr;
}

FSceneOutlinerTreeItemID FVariablesOutlinerDataInterfaceItem::GetID() const
{
	TSoftObjectPtr<UAnimNextRigVMAssetEntry> SoftObjectPtr(WeakEntry.Get());
	return GetTypeHash(SoftObjectPtr);
}

FString FVariablesOutlinerDataInterfaceItem::GetDisplayString() const
{
	UAnimNextRigVMAssetEntry* Entry = WeakEntry.Get();
	if(Entry == nullptr)
	{
		return FString();
	}

	return Entry->GetDisplayName().ToString();
}

TSharedRef<SWidget> FVariablesOutlinerDataInterfaceItem::GenerateLabelWidget(ISceneOutliner& Outliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow)
{
	return SNew(SVariablesOutlinerDataInterfaceLabel, *this, Outliner, InRow);
}

FString FVariablesOutlinerDataInterfaceItem::GetPackageName() const
{
	UAnimNextRigVMAssetEntry* Entry = WeakEntry.Get();
	if(Entry == nullptr)
	{
		return ISceneOutlinerTreeItem::GetPackageName();
	}

	return Entry->GetPackage()->GetName();
}

}

#undef LOCTEXT_NAMESPACE // "VariablesOutlinerTreeItem"