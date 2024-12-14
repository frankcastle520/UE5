// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NiagaraGraph.h"
#include "NiagaraScriptVariable.h"
#include "ViewModels/HierarchyEditor/NiagaraHierarchyViewModelBase.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Views/STreeView.h"

class UNiagaraHierarchyScriptParameter;
class FNiagaraObjectSelection;
class FNiagaraScriptToolkit;
class SComboButton;

class NIAGARAEDITOR_API SNiagaraScriptInputPreviewPanel : public SCompoundWidget, public FGCObject, public FSelfRegisteringEditorUndoClient
{
public:
	SLATE_BEGIN_ARGS(SNiagaraScriptInputPreviewPanel)
		{
		}

	SLATE_END_ARGS()
	
	void Construct(const FArguments& InArgs, TSharedRef<FNiagaraScriptToolkit> InScriptToolkit, TSharedRef<FNiagaraObjectSelection> InVariableObjectSelection);
	virtual ~SNiagaraScriptInputPreviewPanel() override;
	
	void Refresh();
	void SetupDelegates();
	void RemoveDelegates();
private:
	TSharedRef<ITableRow> OnGenerateRow(UNiagaraHierarchyItemBase* Item, const TSharedRef<STableViewBase>& TableViewBase) const;
	void OnGetChildren(UNiagaraHierarchyItemBase* Item, TArray<UNiagaraHierarchyItemBase*>& OutChildren) const;
	void OnParametersChanged(TOptional<UNiagaraGraph::FParametersChangedData> ParametersChangedData);

	FReply SummonHierarchyEditor() const;

	struct FSearchItem
	{
		TArray<UNiagaraHierarchyItemBase*> Path;

		UNiagaraHierarchyItemBase* GetEntry() const
		{
			return Path.Num() > 0 ? 
				Path[Path.Num() - 1] : 
				nullptr;
		}

		bool operator==(const FSearchItem& Item) const
		{
			return Path == Item.Path;
		}
	};

	void OnSearchTextChanged(const FText& Text);
	void OnSearchTextCommitted(const FText& Text, ETextCommit::Type CommitType);
	void OnSearchButtonClicked(SSearchBox::SearchDirection SearchDirection);
	void GenerateSearchItems(UNiagaraHierarchyItemBase* Root, TArray<UNiagaraHierarchyItemBase*> ParentChain, TArray<FSearchItem>& OutSearchItems);
	void ExpandSourceSearchResults();
	void SelectNextSourceSearchResult();
	void SelectPreviousSourceSearchResult();
	TOptional<SSearchBox::FSearchResultData> GetSearchResultData() const;

	/** FGCObject */
	virtual FString GetReferencerName() const override;
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;

	/** FSelfRegisteringEditorUndoClient */
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;
private:
	TArray<UNiagaraHierarchyItemBase*> RootArray;
	TWeakPtr<FNiagaraScriptToolkit> ScriptToolkit;
	TWeakPtr<FNiagaraObjectSelection> VariableObjectSelection;
	TSharedPtr<SSearchBox> SearchBox;
	TSharedPtr<STreeView<UNiagaraHierarchyItemBase*>> TreeView;

	/** We construct and maintain an array of parameters _not_ in the hierarchy to ensure we display all parameters without requiring hierarchy setup. */
	TArray<TObjectPtr<UNiagaraHierarchyScriptParameter>> TransientLeftoverParameters; 
	
	TArray<FSearchItem> SearchResults;
	TOptional<FSearchItem> FocusedSearchResult;
	TSharedPtr<SComboButton> AddParameterButton;
};
