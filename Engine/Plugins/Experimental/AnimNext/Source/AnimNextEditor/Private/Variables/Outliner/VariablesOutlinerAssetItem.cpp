// Copyright Epic Games, Inc. All Rights Reserved.

#include "VariablesOutlinerAssetItem.h"

#include "ISceneOutliner.h"
#include "Styling/SlateColor.h"
#include "Misc/Attribute.h"
#include "Misc/Optional.h"
#include "SceneOutlinerStandaloneTypes.h"
#include "AnimNextEditorModule.h"
#include "AnimNextRigVMAsset.h"
#include "AnimNextRigVMAssetEditorData.h"
#include "AssetToolsModule.h"
#include "AssetViewUtils.h"
#include "EditorUtils.h"
#include "IAssetTools.h"
#include "ScopedTransaction.h"
#include "UncookedOnlyUtils.h"
#include "Styling/SlateIconFinder.h"
#include "Styling/StarshipCoreStyle.h"
#include "UObject/Package.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Images/SThrobber.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/SListView.h"

#define LOCTEXT_NAMESPACE "VariablesOutlinerTreeItem"

namespace UE::AnimNext::Editor
{

const FSceneOutlinerTreeItemType FVariablesOutlinerAssetItem::Type;

class SVariablesOutlinerAssetlabel : FSceneOutlinerCommonLabelData, public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SVariablesOutlinerAssetlabel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, FVariablesOutlinerAssetItem& InTreeItem, ISceneOutliner& SceneOutliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow)
	{
		WeakSceneOutliner = StaticCastSharedRef<ISceneOutliner>(SceneOutliner.AsShared());
		TreeItem = StaticCastSharedRef<FVariablesOutlinerAssetItem>(InTreeItem.AsShared());

		ChildSlot
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(2.0f, 2.0f)
			[
				SNew(SImage)
				.Image(this, &SVariablesOutlinerAssetlabel::GetAssetIcon)
			]
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 2.0f)
			[
				SAssignNew(TextBlock, SInlineEditableTextBlock)
				.Font(FStyleFonts::Get().NormalBold)
				.Text(this, &SVariablesOutlinerAssetlabel::GetDisplayText)
				.HighlightText(SceneOutliner.GetFilterHighlightText())
				.ColorAndOpacity(this, &SVariablesOutlinerAssetlabel::GetForegroundColor)
				.OnTextCommitted(this, &SVariablesOutlinerAssetlabel::OnTextCommited)
				.OnVerifyTextChanged(this, &SVariablesOutlinerAssetlabel::OnVerifyTextChanged)
				.IsReadOnly(this, &SVariablesOutlinerAssetlabel::CanRename)
			]
			+SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.Padding(2.0f, 0.0f, 2.0f, 3.0f)
			.AutoWidth()
			[
				SNew(SImage)
				.Visibility(this, &SVariablesOutlinerAssetlabel::GetDirtyImageVisibility)
				.ToolTipText(this, &SVariablesOutlinerAssetlabel::GetDirtyTooltipText)
				.Image(this, &SVariablesOutlinerAssetlabel::GetDirtyImageBrush)
			]
			+SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.Padding(2.0f, 0.0f)
			.AutoWidth()
			[
				SNew(SThrobber)
				.Visibility(this, &SVariablesOutlinerAssetlabel::GetLoadingIndicatorVisibility)
				.ToolTipText(LOCTEXT("LoadingTooltip", "Asset is loading..."))
			]
		];
	}

	bool CanRename() const
	{
		if (const TSharedPtr<FVariablesOutlinerAssetItem> Item = TreeItem.Pin())
		{
			return Item->SoftAsset.Get() != nullptr;
		}
		return false;
	}

	FText GetDirtyTooltipText() const
	{
		if (const TSharedPtr<FVariablesOutlinerAssetItem> Item = TreeItem.Pin())
		{
			FTextBuilder TextBuilder;
			TextBuilder.AppendLine(LOCTEXT("ModifiedTooltip", "Modified"));

			if(UAnimNextRigVMAsset* Asset = Item->SoftAsset.Get())
			{
				const UPackage* Package = Asset->GetPackage();
				check(Package);
				if(Package->IsDirty())
				{
					TextBuilder.AppendLine(FText::FromName(Package->GetFName()));
				}
			}

			return TextBuilder.ToText();
		}
		return FText::GetEmpty();
	}

	const FSlateBrush* GetDirtyImageBrush() const
	{
		if (const TSharedPtr<FVariablesOutlinerAssetItem> Item = TreeItem.Pin())
		{
			if(UAnimNextRigVMAsset* Asset = Item->SoftAsset.Get())
			{
				bool bIsDirty = false;
				const UPackage* Package = Asset->GetPackage();
				check(Package);
				if(Package->IsDirty())
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
		if (const TSharedPtr<FVariablesOutlinerAssetItem> Item = TreeItem.Pin())
		{
			if(UAnimNextRigVMAsset* Asset = Item->SoftAsset.Get())
			{
				bool bIsDirty = false;
				const UPackage* Package = Asset->GetPackage();
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

	EVisibility GetLoadingIndicatorVisibility() const
	{
		if (const TSharedPtr<FVariablesOutlinerAssetItem> Item = TreeItem.Pin())
		{
			UAnimNextRigVMAsset* Asset = Item->SoftAsset.Get();
			return Asset != nullptr ? EVisibility::Collapsed : EVisibility::Visible;
		}
		return EVisibility::Collapsed;
	}
	
	FText GetDisplayText() const
	{
		if (const TSharedPtr<FVariablesOutlinerAssetItem> Item = TreeItem.Pin())
		{
			return FText::FromString(Item->GetDisplayString());
		}
		return FText();
	}

	const FSlateBrush* GetAssetIcon() const
	{
		if (const TSharedPtr<FVariablesOutlinerAssetItem> Item = TreeItem.Pin())
		{
			UAnimNextRigVMAsset* Asset = Item->SoftAsset.Get();
			return Asset != nullptr ? FSlateIconFinder::FindIconBrushForClass(Asset->GetClass()) : FAppStyle::Get().GetBrush("ClassIcon.Object");
		}
		return FAppStyle::Get().GetBrush("ClassIcon.Object");
	}

	void OnTextCommited(const FText& InLabel, ETextCommit::Type InCommitInfo)
	{
		if(InCommitInfo == ETextCommit::OnEnter)
		{
			if (const TSharedPtr<FVariablesOutlinerAssetItem> Item = TreeItem.Pin())
			{
				Item->Rename(InLabel); 
			}
		}
	}

	bool OnVerifyTextChanged(const FText& InLabel, FText& OutErrorMessage)
	{
		if (const TSharedPtr<FVariablesOutlinerAssetItem> Item = TreeItem.Pin())
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

	TWeakPtr<FVariablesOutlinerAssetItem> TreeItem;
	TSharedPtr<SInlineEditableTextBlock> TextBlock;
};

FVariablesOutlinerAssetItem::FVariablesOutlinerAssetItem(TSoftObjectPtr<UAnimNextRigVMAsset> InAsset)
	: ISceneOutlinerTreeItem(FVariablesOutlinerAssetItem::Type)
	, SoftAsset(InAsset)
{
}

bool FVariablesOutlinerAssetItem::IsValid() const
{
	return !SoftAsset.IsNull();
}

FSceneOutlinerTreeItemID FVariablesOutlinerAssetItem::GetID() const
{
	return GetTypeHash(SoftAsset);
}

FString FVariablesOutlinerAssetItem::GetDisplayString() const
{
	return SoftAsset.GetAssetName();
}

TSharedRef<SWidget> FVariablesOutlinerAssetItem::GenerateLabelWidget(ISceneOutliner& Outliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow)
{
	TSharedRef<SVariablesOutlinerAssetlabel> LabelWidget = SNew(SVariablesOutlinerAssetlabel, *this, Outliner, InRow);
	RenameRequestEvent.BindSP(LabelWidget->TextBlock.Get(), &SInlineEditableTextBlock::EnterEditingMode);
	return LabelWidget;
}

FString FVariablesOutlinerAssetItem::GetPackageName() const
{
	return SoftAsset.GetLongPackageName();
}

void FVariablesOutlinerAssetItem::Rename(const FText& InNewName)
{
	UAnimNextRigVMAsset* Asset = SoftAsset.Get();
	if(Asset == nullptr)
	{
		return;
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	const FString CurrentAssetPath = FPackageName::GetLongPackagePath(Asset->GetPackage()->GetName());
	TArray<FAssetRenameData> AssetsToRename = { FAssetRenameData(Asset, CurrentAssetPath, InNewName.ToString()) };
	AssetToolsModule.Get().RenameAssets(AssetsToRename);
}

bool FVariablesOutlinerAssetItem::ValidateName(const FText& InNewName, FText& OutErrorMessage) const
{
	UAnimNextRigVMAsset* Asset = SoftAsset.Get();
	if(Asset == nullptr)
	{
		OutErrorMessage = LOCTEXT("InvalidAssetError", "Asset is invalid");
		return false;
	}

	FString NewName = InNewName.ToString();
	if (NewName.Len() >= NAME_SIZE)
	{
		OutErrorMessage = LOCTEXT("Error_AssetNameTooLarge", "This asset name is too long. Please choose a shorter name.");
		return false;
	}

	if (Asset->GetFName() != FName(*NewName)) // Deliberately ignore case here to allow case-only renames of existing assets
	{
		const FString PackageName = Asset->GetPackage()->GetPathName() / NewName;
		const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *PackageName, *NewName);

		FText ValidationErrorMsg;
		if (!AssetViewUtils::IsValidObjectPathForCreate(ObjectPath, ValidationErrorMsg))
		{
			OutErrorMessage = ValidationErrorMsg;
			return false;
		}
	}

	FAssetToolsModule& AssetToolsModule = FAssetToolsModule::GetModule();
	TWeakPtr<IAssetTypeActions> WeakAssetTypeActions = AssetToolsModule.Get().GetAssetTypeActionsForClass(UAnimNextRigVMAsset::StaticClass());
	if (TSharedPtr<IAssetTypeActions> AssetTypeActions = WeakAssetTypeActions.Pin())
	{
		if (!AssetTypeActions->CanRename(FAssetData(Asset), &OutErrorMessage))
		{
			return false;
		}
	}

	return true;
}

}

#undef LOCTEXT_NAMESPACE // "VariablesOutlinerTreeItem"