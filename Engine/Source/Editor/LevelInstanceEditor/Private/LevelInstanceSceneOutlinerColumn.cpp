// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelInstanceSceneOutlinerColumn.h"
#include "Styling/SlateIconFinder.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Images/SImage.h"
#include "GameFramework/Actor.h"
#include "LevelInstance/LevelInstanceActor.h"
#include "LevelInstance/LevelInstanceInterface.h"
#include "ActorTreeItem.h"

#define LOCTEXT_NAMESPACE "LevelInstanceColumn"

namespace LevelInstanceColumnPrivate
{
	FName Name("Level Instance");

	const FText ToolTipIsOverriden = LOCTEXT("IsOverridenTooltip", "This actor is overridden.");
	const FText ToolTipIsOverridenAndContainsOverrides = LOCTEXT("IsOverridenAndContainsOverridesTooltip", "This level instance is overridden, and so is at least one of its children.");
	const FText ToolTipContainsOverrides = LOCTEXT("ContainsOverridesTooltip", "At least one child of this level instance is overridden.");

	void GetBrushesAndToolTipForItem(FSceneOutlinerTreeItemRef TreeItem, const FSlateBrush*& OutHasOVerrideBrush, const FSlateBrush*& OutContainsOverrideBrush, const FText*& OutToolTipText)
	{
		OutToolTipText = nullptr;
		OutHasOVerrideBrush = nullptr;
		OutContainsOverrideBrush = nullptr;

		if (const FActorTreeItem* ActorTreeITem = TreeItem->CastTo<FActorTreeItem>())
		{
			if (const AActor* Actor = ActorTreeITem->Actor.Get())
			{				
				if (const ILevelInstanceInterface* LevelInstance = Cast<ILevelInstanceInterface>(Actor); LevelInstance && LevelInstance->GetPropertyOverrideAsset() && !LevelInstance->IsEditing())
				{
					const bool bIsEditable = !Actor->IsInLevelInstance() || Actor->IsInEditLevelInstance();

					OutContainsOverrideBrush = bIsEditable ? FAppStyle::GetBrush("LevelInstance.ColumnOverrideContainerInsideEditable") : FAppStyle::GetBrush("LevelInstance.ColumnOverrideContainerInside");

					if (Actor->HasLevelInstancePropertyOverrides())
					{
						OutHasOVerrideBrush = Actor->HasEditableLevelLevelInstancePropertyOverrides() ? FAppStyle::GetBrush("LevelInstance.ColumnOverrideContainerHereEditable") : FAppStyle::GetBrush("LevelInstance.ColumnOverrideContainerHere");
						OutToolTipText = &ToolTipIsOverridenAndContainsOverrides;
					}
					else
					{
						OutHasOVerrideBrush = bIsEditable ? FAppStyle::GetBrush("LevelInstance.ColumnOverrideContainerEditable") : FAppStyle::GetBrush("LevelInstance.ColumnOverrideContainer");
						OutToolTipText = &ToolTipContainsOverrides;
					}
				}
				else if (Actor->HasLevelInstancePropertyOverrides())
				{
					OutHasOVerrideBrush = Actor->HasEditableLevelLevelInstancePropertyOverrides() ? FAppStyle::GetBrush("LevelInstance.ColumnOverrideHereEditable") : FAppStyle::GetBrush("LevelInstance.ColumnOverrideHere");
					OutToolTipText = &ToolTipIsOverriden;
				}
			}
		}
	}
}

FName FLevelInstanceSceneOutlinerColumn::GetID()
{
	return LevelInstanceColumnPrivate::Name;
}

SHeaderRow::FColumn::FArguments FLevelInstanceSceneOutlinerColumn::ConstructHeaderRowColumn()
{
	return SHeaderRow::Column(GetColumnID())
		.FixedWidth(24.f)
		.HAlignHeader(HAlign_Center)
		.VAlignHeader(VAlign_Center)
		.HAlignCell(HAlign_Center)
		.VAlignCell(VAlign_Center)
		.DefaultTooltip(FText::FromName(GetColumnID()))
		[
			SNew(SImage)
			.ColorAndOpacity(FSlateColor::UseForeground())
			.Image(FSlateIconFinder::FindIconBrushForClass(ALevelInstance::StaticClass()))
		];
}

const TSharedRef<SWidget> FLevelInstanceSceneOutlinerColumn::ConstructRowWidget(FSceneOutlinerTreeItemRef TreeItem, const STableRow<FSceneOutlinerTreeItemPtr>& Row)
{
	if (const FActorTreeItem* ActorTreeITem = TreeItem->CastTo<FActorTreeItem>())
	{
		if (const AActor* Actor = ActorTreeITem->Actor.Get())
		{
			// First overlay slot is optional
			return SNew(SOverlay)
					+SOverlay::Slot()
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(SImage)
						.ColorAndOpacity(FSlateColor::UseForeground())
						.Image_Lambda([TreeItem]()
						{
							const FSlateBrush* OutHasOVerrideBrush;
							const FSlateBrush* OutContainsOverrideBrush;
							const FText* OutToolTipText;
							LevelInstanceColumnPrivate::GetBrushesAndToolTipForItem(TreeItem, OutHasOVerrideBrush, OutContainsOverrideBrush, OutToolTipText);
							return OutContainsOverrideBrush;
						})
					]
					+SOverlay::Slot()
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(SImage)
						.ColorAndOpacity(FSlateColor::UseForeground())
						.Image_Lambda([TreeItem]()
						{
							const FSlateBrush* OutHasOVerrideBrush;
							const FSlateBrush* OutContainsOverrideBrush;
							const FText* OutToolTipText;
							LevelInstanceColumnPrivate::GetBrushesAndToolTipForItem(TreeItem, OutHasOVerrideBrush, OutContainsOverrideBrush, OutToolTipText);
							return OutHasOVerrideBrush;
						})
						.ToolTipText_Lambda([TreeItem]()
						{ 
							const FSlateBrush* OutHasOVerrideBrush;
							const FSlateBrush* OutContainsOverrideBrush;
							const FText* OutToolTipText;
							LevelInstanceColumnPrivate::GetBrushesAndToolTipForItem(TreeItem, OutHasOVerrideBrush, OutContainsOverrideBrush, OutToolTipText);
							return OutToolTipText ? *OutToolTipText : FText();
						})
					];
			
		}
	}
	return SNullWidget::NullWidget;
}

#undef LOCTEXT_NAMESPACE