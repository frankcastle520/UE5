// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetDefinition_DMXControlConsole.h"

#include "Algo/Find.h"
#include "DMXControlConsole.h"
#include "DMXControlConsoleEditorModule.h"
#include "Models/DMXControlConsoleCompactEditorModel.h"
#include "Style/DMXControlConsoleEditorStyle.h"
#include "Toolkits/DMXControlConsoleEditorToolkit.h"
#include "Widgets/Docking/SDockTab.h"


#define LOCTEXT_NAMESPACE "AssetDefinition_DMXControlConsole"

FText UAssetDefinition_DMXControlConsole::GetAssetDisplayName() const
{ 
	return LOCTEXT("AssetDefinition_DMXControlConsole", "DMX Control Console"); 
}

FLinearColor UAssetDefinition_DMXControlConsole::GetAssetColor() const
{ 
	return FLinearColor(FColor(62, 140, 35)); 
}

TSoftClassPtr<UObject> UAssetDefinition_DMXControlConsole::GetAssetClass() const
{ 
	return UDMXControlConsole::StaticClass(); 
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_DMXControlConsole::GetAssetCategories() const
{
	static const auto Categories = { FDMXControlConsoleEditorModule::Get().GetControlConsoleCategory() };
	return Categories;
}

EAssetCommandResult UAssetDefinition_DMXControlConsole::OpenAssets(const FAssetOpenArgs& OpenArgs) const
{
	using namespace UE::DMX::Private;

	const FDMXControlConsoleEditorModule& EditorModule = FModuleManager::GetModuleChecked<FDMXControlConsoleEditorModule>(TEXT("DMXControlConsoleEditor"));
	const TSharedPtr<SDockTab> CompactEditorTab = EditorModule.GetCompactEditorTab();

	const UDMXControlConsoleCompactEditorModel* CompactEditorModel = GetDefault<UDMXControlConsoleCompactEditorModel>();

	const TArray<UDMXControlConsole*> ControlConsoles = OpenArgs.LoadObjects<UDMXControlConsole>();
	UDMXControlConsole* const* ConsoleUsedInCompactEditorPtr = Algo::FindByPredicate(ControlConsoles, [CompactEditorModel](UDMXControlConsole* PossibleCompactConsole)
		{
			return CompactEditorModel->IsUsingControlConsole(PossibleCompactConsole);
		});
	if (ConsoleUsedInCompactEditorPtr && CompactEditorTab.IsValid())
	{
		// Restore the full editor if the control console is displayed in the compact editor
		CompactEditorTab->SetContent(SNullWidget::NullWidget);
	}

	for (UDMXControlConsole* DMXControlConsole : ControlConsoles)
	{
		TSharedRef<FDMXControlConsoleEditorToolkit> NewEditor(MakeShared<FDMXControlConsoleEditorToolkit>());
		NewEditor->InitControlConsoleEditor(OpenArgs.GetToolkitMode(), OpenArgs.ToolkitHost, DMXControlConsole);
	}

	return EAssetCommandResult::Handled;
}

const FSlateBrush* UAssetDefinition_DMXControlConsole::GetThumbnailBrush(const FAssetData& InAssetData, const FName InClassName) const
{
	return FDMXControlConsoleEditorStyle::Get().GetBrush("DMXControlConsole.TabIcon");
}

const FSlateBrush* UAssetDefinition_DMXControlConsole::GetIconBrush(const FAssetData& InAssetData, const FName InClassName) const
{
	return FDMXControlConsoleEditorStyle::Get().GetBrush("DMXControlConsole.TabIcon");
}

#undef LOCTEXT_NAMESPACE
