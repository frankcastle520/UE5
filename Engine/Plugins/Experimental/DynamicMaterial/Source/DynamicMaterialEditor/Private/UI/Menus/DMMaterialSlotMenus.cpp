// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/Menus/DMMaterialSlotMenus.h"

#include "Templates/SharedPointer.h"
#include "ToolMenus.h"
#include "UI/Menus/DMMaterialSlotLayerMenus.h"
#include "UI/Menus/DMMenuContext.h"
#include "UI/Widgets/Editor/SDMMaterialSlotEditor.h"
#include "Widgets/SWidget.h"

#define LOCTEXT_NAMESPACE "FDMMaterialSlotMenus"

namespace UE::DynamicMaterialEditor::Private
{
	static const FName SlotAddLayerMenuName = TEXT("MaterialDesigner.MaterialSlot.AddLayer");
}

TSharedRef<SWidget> FDMMaterialSlotMenus::MakeAddLayerButtonMenu(const TSharedPtr<SDMMaterialSlotEditor>& InSlotWidget)
{
	using namespace UE::DynamicMaterialEditor::Private;

	if (!UToolMenus::Get()->IsMenuRegistered(SlotAddLayerMenuName))
	{
		UToolMenu* NewToolMenu = UDMMenuContext::GenerateContextMenuDefault(SlotAddLayerMenuName);

		if (!NewToolMenu)
		{
			return SNullWidget::NullWidget;
		}

		NewToolMenu->AddDynamicSection(NAME_None, FNewToolMenuDelegate::CreateStatic(&FDMMaterialSlotLayerMenus::AddAddLayerSection));
	}

	FToolMenuContext MenuContext(UDMMenuContext::CreateEditor(InSlotWidget->GetEditorWidget()));

	return UToolMenus::Get()->GenerateWidget(SlotAddLayerMenuName, MenuContext);
}

#undef LOCTEXT_NAMESPACE
