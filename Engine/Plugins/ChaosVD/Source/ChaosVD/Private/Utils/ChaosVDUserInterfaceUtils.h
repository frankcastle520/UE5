// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "ChaosVDSettingsManager.h"
#include "IStructureDetailsView.h"
#include "SEnumCombo.h"
#include "ToolMenu.h"
#include "ToolMenuSection.h"
#include "Widgets/SChaosVDEnumFlagsMenu.h"

class UToolMenu;

namespace Chaos::VisualDebugger::Utils
{
	TSharedRef<IStructureDetailsView> MakeStructDetailsViewForMenu();

	TSharedRef<IDetailsView> MakeObjectDetailsViewForMenu();

	template <typename EnumType>
	TSharedRef<SWidget> MakeEnumMenuEntryWidget(const FText& MenuEntryLabel, const SEnumComboBox::FOnEnumSelectionChanged&& EnumValueChanged, const TAttribute<int32>&& CurrentValueAttribute)
	{
		return SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(4.0f, 0.f)
				[
					SNew(STextBlock)
					.Text(MenuEntryLabel)
					.Font(FAppStyle::GetFontStyle(TEXT("MenuItem.Font")))
				]
				+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SEnumComboBox, StaticEnum<EnumType>())
					.CurrentValue(CurrentValueAttribute)
					.OnEnumSelectionChanged(EnumValueChanged)
				];
	}

	UENUM()
	enum class EChaosVDSaveSettingsOptions
	{
		None = 0,
		ShowSaveButton = 1 << 0,
		ShowResetButton = 1 << 1
	};
	ENUM_CLASS_FLAGS(EChaosVDSaveSettingsOptions)

	void CreateMenuEntryForObject(UToolMenu* Menu, UObject* Object, EChaosVDSaveSettingsOptions MenuEntryOptions = EChaosVDSaveSettingsOptions::None);

	template <typename Object>
	void CreateMenuEntryForSettingsObject(UToolMenu* Menu, EChaosVDSaveSettingsOptions MenuEntryOptions = EChaosVDSaveSettingsOptions::None)
	{
		CreateMenuEntryForObject(Menu, FChaosVDSettingsManager::Get().GetSettingsObject<Object>(), MenuEntryOptions);
	}

	template <typename TStruct>
	void SetStructToDetailsView(TStruct* NewStruct, TSharedRef<IStructureDetailsView>& InDetailsView)
	{
		TSharedPtr<FStructOnScope> StructDataView = nullptr;

		if (NewStruct)
		{
			StructDataView = MakeShared<FStructOnScope>(TStruct::StaticStruct(), reinterpret_cast<uint8*>(NewStruct));
		}

		InDetailsView->SetStructureData(StructDataView);
	}

	template <typename ObjectSettingsType, typename VisualizationFlagsType>
	void CreateVisualizationOptionsMenuSections(UToolMenu* Menu, FName SectionName, const FText& InSectionLabel, const FText& InFlagsMenuLabel,  const FText& InFlagsMenuTooltip, FSlateIcon FlagsMenuIcon,  const FText& InSettingsMenuLabel, const FText& InSettingsMenuTooltip)
	{
		FToolMenuSection& Section = Menu->AddSection(SectionName, InSectionLabel);
		
		Section.AddSubMenu(FName(InSectionLabel.ToString()), InFlagsMenuLabel, InFlagsMenuTooltip, FNewToolMenuDelegate::CreateLambda([](UToolMenu* Menu)
						   {
							   TSharedRef<SWidget> VisualizationFlagsWidget = SNew(SChaosVDEnumFlagsMenu<VisualizationFlagsType>)
								   .CurrentValue_Static(&ObjectSettingsType::GetDataVisualizationFlags)
								   .OnEnumSelectionChanged_Static(&ObjectSettingsType::SetDataVisualizationFlags);
			
							   FToolMenuEntry FlagsMenuEntry = FToolMenuEntry::InitWidget("VisualizationFlags", VisualizationFlagsWidget,FText::GetEmpty());
							   Menu->AddMenuEntry(NAME_None, FlagsMenuEntry);
						   }),
						   false, FlagsMenuIcon);

		using namespace Chaos::VisualDebugger::Utils;
		Section.AddSubMenu(FName(InSettingsMenuLabel.ToString()), InSettingsMenuLabel, InSettingsMenuTooltip, FNewToolMenuDelegate::CreateStatic(&CreateMenuEntryForSettingsObject<ObjectSettingsType>, EChaosVDSaveSettingsOptions::ShowResetButton),
						   false, FSlateIcon(FAppStyle::Get().GetStyleSetName(), TEXT("Icons.Toolbar.Settings")));
	}
}
