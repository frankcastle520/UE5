// Copyright Epic Games, Inc. All Rights Reserved.

#include "SAvaTransitionTreeInstancingModeSelector.h"

#include "AvaTransitionTree.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SComboBox.h"

#define LOCTEXT_NAMESPACE "AvaTransitionTreeInstancingModeSelector"

void SAvaTransitionTreeInstancingModeSelector::Construct(const FArguments& InArgs, UAvaTransitionTree* InTransitionTree)
{
	UpdateItems();
	TransitionTreeWeak = InTransitionTree;

	ChildSlot
	[
		SAssignNew(Combo, SComboBox<FName>)
		.InitiallySelectedItem(GetItemFromProperty())
		.OptionsSource(&Items)
		.OnGenerateWidget(this, &SAvaTransitionTreeInstancingModeSelector::GenerateWidget)
		.OnSelectionChanged(this, &SAvaTransitionTreeInstancingModeSelector::HandleSelectionChanged)
		[
			SNew(STextBlock)
			.Text(this, &SAvaTransitionTreeInstancingModeSelector::GetDisplayTextFromProperty)
		]
	];
}
	
TSharedRef<SWidget> SAvaTransitionTreeInstancingModeSelector::GenerateWidget(FName InItem) const
{
	return SNew(STextBlock)
		.Text(GetDisplayTextFromItem(InItem));
}

void SAvaTransitionTreeInstancingModeSelector::HandleSelectionChanged(FName InProposedSelection, ESelectInfo::Type InSelectInfo)
{
	if (UAvaTransitionTree* TransitionTree = TransitionTreeWeak.Get())
	{
		FScopedTransaction Transaction(LOCTEXT("SetTransitionTreeInstancingMode", "Set Transition Tree Instancing Mode")); 
		TransitionTree->Modify();
		int64 Value = StaticEnum<EAvaTransitionInstancingMode>()->GetValueByName(InProposedSelection); 
		TransitionTree->SetInstancingMode(static_cast<EAvaTransitionInstancingMode>(Value));
	}
}

FText SAvaTransitionTreeInstancingModeSelector::GetDisplayTextFromProperty() const
{
	if (const UAvaTransitionTree* TransitionTree = TransitionTreeWeak.Get())
	{
		return StaticEnum<EAvaTransitionInstancingMode>()->GetDisplayNameTextByValue(static_cast<int64>(TransitionTree->GetInstancingMode()));
	}
	return FText::GetEmpty();
}

FName SAvaTransitionTreeInstancingModeSelector::GetItemFromProperty() const
{
	if (const UAvaTransitionTree* TransitionTree = TransitionTreeWeak.Get())
	{
		return StaticEnum<EAvaTransitionInstancingMode>()->GetNameByValue(static_cast<int64>(TransitionTree->GetInstancingMode()));
	}
	return NAME_None;
}

FText SAvaTransitionTreeInstancingModeSelector::GetDisplayTextFromItem(FName InItem) const
{
	const int64 Value = StaticEnum<EAvaTransitionInstancingMode>()->GetValueByName(InItem);
	return StaticEnum<EAvaTransitionInstancingMode>()->GetDisplayNameTextByValue(Value);
}

void SAvaTransitionTreeInstancingModeSelector::UpdateItems()
{
	const UEnum* InstancingModeEnum = StaticEnum<EAvaTransitionInstancingMode>();

	Items.Empty(InstancingModeEnum->NumEnums() - 1);
	for (int32 Index = 0; Index < InstancingModeEnum->NumEnums() - 1; ++Index)
	{
		Items.Add(InstancingModeEnum->GetNameByIndex(Index));
	}
}

#undef LOCTEXT_NAMESPACE