// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraGraph.h"
#include "ViewModels/HierarchyEditor/NiagaraHierarchyViewModelBase.h"
#include "NiagaraScriptVariable.h"
#include "ViewModels/NiagaraScriptViewModel.h"
#include "NiagaraHierarchyScriptParametersViewModel.generated.h"

class UNiagaraGraph;

/** The refresh context is used to determine if hierarchy script variables should be removed. */
UCLASS()
class UNiagaraHierarchyScriptParameterRefreshContext : public UNiagaraHierarchyDataRefreshContext
{
	GENERATED_BODY()

public:
	void SetNiagaraGraph(UNiagaraGraph* InGraph) { NiagaraGraph = InGraph; }
	const UNiagaraGraph* GetNiagaraGraph() const { return NiagaraGraph; }
private:
	UPROPERTY(Transient)
	TObjectPtr<UNiagaraGraph> NiagaraGraph;
};

/** A hierarchy script parameter is an optional object embedded in the hierarchy. */
UCLASS()
class UNiagaraHierarchyScriptParameter : public UNiagaraHierarchyItem
{
	GENERATED_BODY()

public:
	void Initialize(UNiagaraScriptVariable& InParameterScriptVariable);

	virtual FString ToString() const override { return GetVariable().GetName().ToString(); }
	FText GetTooltip() const { return ParameterScriptVariable->Metadata.Description; }
	
	UNiagaraScriptVariable* GetScriptVariable() const { return ParameterScriptVariable; }
	FNiagaraVariable GetVariable() const { return ParameterScriptVariable->Variable; }
private:
	UPROPERTY()
	TObjectPtr<UNiagaraScriptVariable> ParameterScriptVariable;
};

/** The category class used for the script hierarchy editor. It lets us add additional data later on. */
UCLASS()
class UNiagaraHierarchyScriptCategory : public UNiagaraHierarchyCategory
{
	GENERATED_BODY()
};

/**
 * The view model that defines the script editor's hierarchy editor for input parameters.
 */
UCLASS()
class NIAGARAEDITOR_API UNiagaraHierarchyScriptParametersViewModel : public UNiagaraHierarchyViewModelBase
{
	GENERATED_BODY()
public:
	void Initialize(TSharedRef<FNiagaraScriptViewModel> InScriptViewModel);
	TSharedRef<FNiagaraScriptViewModel> GetScriptViewModel() const;

	/** UNiagaraHierarchyViewModelBase */
	virtual UNiagaraHierarchyRoot* GetHierarchyRoot() const override;
	virtual TSubclassOf<UNiagaraHierarchyCategory> GetCategoryDataClass() const override;
	virtual TSharedPtr<FNiagaraHierarchyRootViewModel> CreateRootViewModelForData(UNiagaraHierarchyRoot* Root, bool bIsForHierarchy) override;
	virtual TSharedPtr<FNiagaraHierarchyItemViewModelBase> CreateViewModelForData(UNiagaraHierarchyItemBase* ItemBase, TSharedPtr<FNiagaraHierarchyItemViewModelBase> Parent) override;
	virtual void PrepareSourceItems(UNiagaraHierarchyRoot* SourceRoot, TSharedPtr<FNiagaraHierarchyRootViewModel> SourceRootViewModel) override;
	virtual void SetupCommands() override;
	virtual TSharedRef<FNiagaraHierarchyDragDropOp> CreateDragDropOp(TSharedRef<FNiagaraHierarchyItemViewModelBase> Item) override;
	virtual bool SupportsDetailsPanel() override { return true; }
	virtual void FinalizeInternal() override;
private:
	void OnParametersChanged(TOptional<UNiagaraGraph::FParametersChangedData> ParametersChangedData);
private:
	TWeakPtr<FNiagaraScriptViewModel> ScriptViewModelWeak;
};

class FNiagaraHierarchyScriptParameterDragDropOp : public FNiagaraHierarchyDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FNiagaraHierarchyScriptParameterDragDropOp, FNiagaraHierarchyDragDropOp)

	FNiagaraHierarchyScriptParameterDragDropOp(TSharedPtr<FNiagaraHierarchyItemViewModel> InputViewModel) : FNiagaraHierarchyDragDropOp(InputViewModel) {}
	
	virtual TSharedRef<SWidget> CreateCustomDecorator() const override;
};

struct FNiagaraHierarchyScriptParameterViewModel : public FNiagaraHierarchyItemViewModel
{
	FNiagaraHierarchyScriptParameterViewModel(UNiagaraHierarchyScriptParameter* ScriptParameter, TSharedPtr<FNiagaraHierarchyItemViewModelBase> InParent, TWeakObjectPtr<UNiagaraHierarchyScriptParametersViewModel> ViewModel, bool bInIsForHierarchy)
		: FNiagaraHierarchyItemViewModel(ScriptParameter, InParent, ViewModel, bInIsForHierarchy)	{}

protected:
	virtual bool RepresentsExternalData() const override { return true; }
	virtual bool DoesExternalDataStillExist(const UNiagaraHierarchyDataRefreshContext* Context) const override;
	virtual UObject* GetDataForEditing() override { return Cast<UNiagaraHierarchyScriptParameter>(GetDataMutable())->GetScriptVariable(); }
	/** We want to be able to edit in the details panel regardless of source or hierarchy item. */
	virtual bool AllowEditingInDetailsPanel() const override { return true; }
	virtual bool CanRenameInternal() override { return false; }
	
	virtual FCanPerformActionResults CanDropOnInternal(TSharedPtr<FNiagaraHierarchyItemViewModelBase>, EItemDropZone ItemDropZone) override;
	virtual void OnDroppedOnInternal(TSharedPtr<FNiagaraHierarchyItemViewModelBase> DroppedItem, EItemDropZone ItemDropZone) override;
	
	virtual bool CanHaveChildren() const override { return bIsForHierarchy == true; }
};

struct FNiagaraHierarchyScriptRootViewModel : public FNiagaraHierarchyRootViewModel
{
	FNiagaraHierarchyScriptRootViewModel(UNiagaraHierarchyRoot* Root, TWeakObjectPtr<UNiagaraHierarchyScriptParametersViewModel> ViewModel, bool bInIsForHierarchy)
		: FNiagaraHierarchyRootViewModel(Root, ViewModel, bInIsForHierarchy)	{}

protected:
	// In the script, loose parameters are always added before categories (reverse of the default implementation)
	virtual void SortChildrenData() const override;
};
