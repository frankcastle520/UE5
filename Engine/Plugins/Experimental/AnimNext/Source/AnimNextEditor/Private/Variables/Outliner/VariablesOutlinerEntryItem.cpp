// Copyright Epic Games, Inc. All Rights Reserved.

#include "VariablesOutlinerEntryItem.h"

#include "ISceneOutliner.h"
#include "Styling/SlateColor.h"
#include "Misc/Attribute.h"
#include "Misc/Optional.h"
#include "SceneOutlinerStandaloneTypes.h"
#include "AnimNextEditorModule.h"
#include "AnimNextRigVMAssetEditorData.h"
#include "EditorUtils.h"
#include "Variables/IAnimNextRigVMVariableInterface.h"
#include "ScopedTransaction.h"
#include "UncookedOnlyUtils.h"
#include "Entries/AnimNextRigVMAssetEntry.h"
#include "UObject/Package.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/SListView.h"
#include "EdGraphSchema_K2.h"
#include "Entries/AnimNextVariableEntry.h"

#define LOCTEXT_NAMESPACE "VariablesOutlinerTreeItem"

namespace UE::AnimNext::Editor
{

const FSceneOutlinerTreeItemType FVariablesOutlinerEntryItem::Type;

class SVariablesOutlinerEntrylabel : FSceneOutlinerCommonLabelData, public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SVariablesOutlinerEntrylabel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, FVariablesOutlinerEntryItem& InTreeItem, ISceneOutliner& SceneOutliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow)
	{
		WeakSceneOutliner = StaticCastSharedRef<ISceneOutliner>(SceneOutliner.AsShared());
		TreeItem = StaticCastSharedRef<FVariablesOutlinerEntryItem>(InTreeItem.AsShared());

		ChildSlot
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 2.0f)
			[
				SAssignNew(TextBlock, SInlineEditableTextBlock)
				.Text(this, &SVariablesOutlinerEntrylabel::GetDisplayText)
				.HighlightText(SceneOutliner.GetFilterHighlightText())
				.ColorAndOpacity(this, &SVariablesOutlinerEntrylabel::GetForegroundColor)
				.OnTextCommitted(this, &SVariablesOutlinerEntrylabel::OnTextCommited)
				.OnVerifyTextChanged(this, &SVariablesOutlinerEntrylabel::OnVerifyTextChanged)
			]
			+SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.Padding(2.0f, 0.0f, 2.0f, 3.0f)
			.AutoWidth()
			[
				SNew(SImage)
				.ToolTipText(this, &SVariablesOutlinerEntrylabel::GetDirtyTooltipText)
				.Image(this, &SVariablesOutlinerEntrylabel::GetDirtyImageBrush)
			]
		];
	}

	FText GetDirtyTooltipText() const
	{
		if (const TSharedPtr<FVariablesOutlinerEntryItem> Item = TreeItem.Pin())
		{
			FTextBuilder TextBuilder;
			TextBuilder.AppendLine(LOCTEXT("ModifiedTooltip", "Modified"));

			if(UAnimNextVariableEntry* AssetEntry = Item->WeakEntry.Get())
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
		if (const TSharedPtr<FVariablesOutlinerEntryItem> Item = TreeItem.Pin())
		{
			if(UAnimNextVariableEntry* AssetEntry = Item->WeakEntry.Get())
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
	
	FText GetDisplayText() const
	{
		if (const TSharedPtr<FVariablesOutlinerEntryItem> Item = TreeItem.Pin())
		{
			return FText::FromString(Item->GetDisplayString());
		}
		return FText();
	}

	void OnTextCommited(const FText& InLabel, ETextCommit::Type InCommitInfo)
	{
		if(InCommitInfo == ETextCommit::OnEnter)
		{
			if (const TSharedPtr<FVariablesOutlinerEntryItem> Item = TreeItem.Pin())
			{
				Item->Rename(InLabel); 
			}
		}
	}

	bool OnVerifyTextChanged(const FText& InLabel, FText& OutErrorMessage)
	{
		if (const TSharedPtr<FVariablesOutlinerEntryItem> Item = TreeItem.Pin())
		{
			return Item->ValidateName(InLabel, OutErrorMessage); 
		}
		return false;
	}

	virtual FSlateColor GetForegroundColor() const override
	{
		const TOptional<FLinearColor> BaseColor = FSceneOutlinerCommonLabelData::GetForegroundColor(*TreeItem.Pin());
		return BaseColor.IsSet() ? BaseColor.GetValue() : FSlateColor::UseForeground();
	}

	TWeakPtr<FVariablesOutlinerEntryItem> TreeItem;
	TSharedPtr<SInlineEditableTextBlock> TextBlock;
};

FVariablesOutlinerEntryItem::FVariablesOutlinerEntryItem(UAnimNextVariableEntry* InEntry)
	: ISceneOutlinerTreeItem(FVariablesOutlinerEntryItem::Type)
	, WeakEntry(InEntry)
{
}

bool FVariablesOutlinerEntryItem::IsValid() const
{
	return WeakEntry.Get() != nullptr;
}

FSceneOutlinerTreeItemID FVariablesOutlinerEntryItem::GetID() const
{
	return HashCombine(GetTypeHash(WeakEntry), GetTypeHash(WeakDataInterfaceEntry));
}

FString FVariablesOutlinerEntryItem::GetDisplayString() const
{
	UAnimNextRigVMAssetEntry* Entry = WeakEntry.Get();
	if(Entry == nullptr)
	{
		return FString();
	}

	return Entry->GetDisplayName().ToString();
}

TSharedRef<SWidget> FVariablesOutlinerEntryItem::GenerateLabelWidget(ISceneOutliner& Outliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow)
{
	TSharedRef<SVariablesOutlinerEntrylabel> LabelWidget = SNew(SVariablesOutlinerEntrylabel, *this, Outliner, InRow);
	RenameRequestEvent.BindSP(LabelWidget->TextBlock.Get(), &SInlineEditableTextBlock::EnterEditingMode);
	return LabelWidget;
}

FString FVariablesOutlinerEntryItem::GetPackageName() const
{
	UAnimNextRigVMAssetEntry* Entry = WeakEntry.Get();
	if(Entry == nullptr)
	{
		return ISceneOutlinerTreeItem::GetPackageName();
	}

	return Entry->GetPackage()->GetName();
}

void FVariablesOutlinerEntryItem::Rename(const FText& InNewName)
{
	UAnimNextRigVMAssetEntry* Entry = WeakEntry.Get();
	if(Entry == nullptr)
	{
		return;
	}

	FScopedTransaction Transaction(LOCTEXT("RenameVariable", "Rename variable"));
	Entry->SetEntryName(FName(*InNewName.ToString()));
}

bool FVariablesOutlinerEntryItem::ValidateName(const FText& InNewName, FText& OutErrorMessage) const
{
	UAnimNextVariableEntry* Entry = WeakEntry.Get();
	if(Entry == nullptr)
	{
		OutErrorMessage = LOCTEXT("InvalidVariableError", "Variable is invalid");
		return false;
	}

	UAnimNextRigVMAssetEditorData* EditorData = Entry->GetTypedOuter<UAnimNextRigVMAssetEditorData>();
	if(EditorData == nullptr)
	{
		OutErrorMessage = LOCTEXT("InvalidEditorDataError", "Variable has invalid editor data");
		return false;
	}

	const FString NewString = InNewName.ToString();
	if (!Editor::FUtils::IsValidParameterNameString(NewString, OutErrorMessage))
	{
		return false;
	}

	FName Name(*NewString);
	if(UAnimNextRigVMAssetEntry* ExistingEntry = EditorData->FindEntry(Name))
	{
		OutErrorMessage = LOCTEXT("NameExistsError", "Variable name already exists in this asset");
		return false;
	}

	return true;
}

}

#undef LOCTEXT_NAMESPACE // "VariablesOutlinerTreeItem"