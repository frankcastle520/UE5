// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ViewModels/Stack/NiagaraStackScriptHierarchyRoot.h"
#include "Styling/SlateTypes.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Input/SCheckBox.h"

class SNiagaraStackHierarchySection : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNiagaraStackHierarchySection) {}
	SLATE_EVENT(FOnCheckStateChanged, OnCheckStateChanged)
	SLATE_ATTRIBUTE(ECheckBoxState, IsChecked)
SLATE_END_ARGS()
	
	void Construct(const FArguments& InArgs, const UNiagaraHierarchySection* InSection);

private:
	FText GetSectionNameAsText() const;
	FText GetTooltipText() const;
private:
	TWeakObjectPtr<const UNiagaraHierarchySection> Section;
};

class SNiagaraStackScriptHierarchyRoot : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNiagaraStackScriptHierarchyRoot) {}
	SLATE_END_ARGS();

	void Construct(const FArguments& InArgs, UNiagaraStackScriptHierarchyRoot* InModuleHierarchyRoot);

private:
	void ConstructSectionButtons();
	
	EVisibility GetLabelVisibility() const;

	ECheckBoxState IsSectionChecked(const UNiagaraHierarchySection* NiagaraHierarchySection) const;
	void OnCheckStateChanged(ECheckBoxState CheckBoxState, const UNiagaraHierarchySection* NiagaraHierarchySection);

	void HierarchyStructureChanged(ENiagaraStructureChangedFlags StructureChangedFlags);

private:
	TWeakObjectPtr<UNiagaraStackScriptHierarchyRoot> ModuleHierarchyRoot;

	TSharedPtr<SWrapBox> SectionSelectorBox;
};