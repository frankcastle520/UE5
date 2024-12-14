// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewModels/HierarchyEditor/NiagaraHierarchyScriptParametersViewModel.h"

#include "NiagaraConstants.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraGraph.h"
#include "NiagaraScriptSource.h"
#include "ViewModels/NiagaraScriptViewModel.h"

#define LOCTEXT_NAMESPACE "NiagaraScriptParameterHierarchyEditor"

void UNiagaraHierarchyScriptParameter::Initialize(UNiagaraScriptVariable& InParameterScriptVariable)
{
	ParameterScriptVariable = &InParameterScriptVariable;
	SetIdentity(FNiagaraHierarchyIdentity({InParameterScriptVariable.Metadata.GetVariableGuid()}, {}));
}

void UNiagaraHierarchyScriptParametersViewModel::Initialize(TSharedRef<FNiagaraScriptViewModel> InScriptViewModel)
{
	ScriptViewModelWeak = InScriptViewModel;

	Cast<UNiagaraScriptSource>(ScriptViewModelWeak.Pin()->GetStandaloneScript().GetScriptData()->GetSource())->NodeGraph->OnParametersChanged().AddUObject(this, &UNiagaraHierarchyScriptParametersViewModel::OnParametersChanged);
	UNiagaraHierarchyViewModelBase::Initialize();

	UNiagaraHierarchyScriptParameterRefreshContext* ScriptParameterRefreshContext = NewObject<UNiagaraHierarchyScriptParameterRefreshContext>(this);
	ScriptParameterRefreshContext->SetNiagaraGraph(Cast<UNiagaraScriptSource>(InScriptViewModel->GetStandaloneScript().GetScriptData()->GetSource())->NodeGraph);
	SetRefreshContext(ScriptParameterRefreshContext);
}

TSharedRef<FNiagaraScriptViewModel> UNiagaraHierarchyScriptParametersViewModel::GetScriptViewModel() const
{
	TSharedPtr<FNiagaraScriptViewModel> ScriptViewModelPinned = ScriptViewModelWeak.Pin();
	checkf(ScriptViewModelPinned.IsValid(), TEXT("Script view model destroyed before parameters hierarchy view model."));
	return ScriptViewModelPinned.ToSharedRef();
}


UNiagaraHierarchyRoot* UNiagaraHierarchyScriptParametersViewModel::GetHierarchyRoot() const
{
	const TArray<FVersionedNiagaraScriptWeakPtr>& Scripts = GetScriptViewModel()->GetScripts();
	if(ensure(Scripts.Num() >= 1 && Scripts[0].Pin().Script != nullptr) == false)
	{
		return nullptr;
	}

	FVersionedNiagaraScriptData* ScriptData = Scripts[0].Pin().GetScriptData();
	if(ensure(ScriptData != nullptr) == false)
	{
		return nullptr;
	}

	return Cast<UNiagaraScriptSource>(ScriptData->GetSource())->NodeGraph->GetScriptParameterHierarchyRoot();
}

TSubclassOf<UNiagaraHierarchyCategory> UNiagaraHierarchyScriptParametersViewModel::GetCategoryDataClass() const
{
	return UNiagaraHierarchyScriptCategory::StaticClass();
}

TSharedPtr<FNiagaraHierarchyRootViewModel> UNiagaraHierarchyScriptParametersViewModel::CreateRootViewModelForData(UNiagaraHierarchyRoot* Root, bool bIsForHierarchy)
{
	return MakeShared<FNiagaraHierarchyScriptRootViewModel>(Root, this, bIsForHierarchy);
}

TSharedPtr<FNiagaraHierarchyItemViewModelBase> UNiagaraHierarchyScriptParametersViewModel::CreateViewModelForData(UNiagaraHierarchyItemBase* ItemBase, TSharedPtr<FNiagaraHierarchyItemViewModelBase> Parent)
{
	if(UNiagaraHierarchyScriptParameter* Item = Cast<UNiagaraHierarchyScriptParameter>(ItemBase))
	{
		return MakeShared<FNiagaraHierarchyScriptParameterViewModel>(Item, Parent, this, Parent.IsValid() ? Parent->IsForHierarchy() : false);
	}
	else if(UNiagaraHierarchyCategory* Category = Cast<UNiagaraHierarchyCategory>(ItemBase))
	{
		return MakeShared<FNiagaraHierarchyCategoryViewModel>(Category, Parent, this, Parent.IsValid() ? Parent->IsForHierarchy() : false);
	}

	return nullptr;
}

void UNiagaraHierarchyScriptParametersViewModel::PrepareSourceItems(UNiagaraHierarchyRoot* SourceRoot, TSharedPtr<FNiagaraHierarchyRootViewModel> SourceRootViewModel)
{
	UNiagaraScriptSourceBase* SourceBase = GetScriptViewModel()->GetStandaloneScript().GetScriptData()->GetSource();
	UNiagaraScriptSource* ScriptSource = Cast<UNiagaraScriptSource>(SourceBase);
	const TMap<FNiagaraVariable, TObjectPtr<UNiagaraScriptVariable>>& ScriptVariableMap = ScriptSource->NodeGraph->GetAllMetaData();

	TArray<UNiagaraHierarchyItemBase*> OldChildren = SourceRoot->GetChildrenMutable();
	SourceRoot->GetChildrenMutable().Empty();

	for(const auto& ScriptVariablePair : ScriptVariableMap)
	{
		// We only want to be able to organize module inputs & static switches 
		if(ScriptVariablePair.Key.IsInNameSpace(FNiagaraConstants::ModuleNamespace) == false && ScriptVariablePair.Value->GetIsStaticSwitch() == false)
		{
			continue;
		}
		
		UNiagaraScriptVariable* ScriptVariable = ScriptVariablePair.Value;
		if(UNiagaraHierarchyItemBase** ExistingChild = OldChildren.FindByPredicate([ScriptVariable](UNiagaraHierarchyItemBase* ItemBase)
		{
			return ItemBase->GetPersistentIdentity().Guids[0] == ScriptVariable->Metadata.GetVariableGuid();
		}))
		{
			SourceRoot->GetChildrenMutable().Add(*ExistingChild);
			continue;
		}

		// since the source items are transient we need to create them here and keep them around until the end of the tool's lifetime
		UNiagaraHierarchyScriptParameter* ScriptParameterHierarchyObject = NewObject<UNiagaraHierarchyScriptParameter>(SourceRoot);
		ScriptParameterHierarchyObject->Initialize(*ScriptVariable);
		SourceRoot->GetChildrenMutable().Add(ScriptParameterHierarchyObject);
	}
}

void UNiagaraHierarchyScriptParametersViewModel::SetupCommands()
{
	Super::SetupCommands();
}

TSharedRef<FNiagaraHierarchyDragDropOp> UNiagaraHierarchyScriptParametersViewModel::CreateDragDropOp(TSharedRef<FNiagaraHierarchyItemViewModelBase> Item)
{
	if(UNiagaraHierarchyCategory* HierarchyCategory = Cast<UNiagaraHierarchyCategory>(Item->GetDataMutable()))
	{
		TSharedRef<FNiagaraHierarchyDragDropOp> CategoryDragDropOp = MakeShared<FNiagaraHierarchyDragDropOp>(Item);
		CategoryDragDropOp->SetAdditionalLabel(FText::FromString(Item->ToString()));
		CategoryDragDropOp->Construct();
		return CategoryDragDropOp;
	}
	else if(UNiagaraHierarchyScriptParameter* ScriptParameter = Cast<UNiagaraHierarchyScriptParameter>(Item->GetDataMutable()))
	{
		TSharedPtr<FNiagaraHierarchyItemViewModel> ScriptParameterViewModel = StaticCastSharedRef<FNiagaraHierarchyItemViewModel>(Item);
		TSharedRef<FNiagaraHierarchyDragDropOp> ScriptParameterDragDropOp = MakeShared<FNiagaraHierarchyScriptParameterDragDropOp>(ScriptParameterViewModel);
		ScriptParameterDragDropOp->Construct();
		return ScriptParameterDragDropOp;
	}
	
	check(false);
	return MakeShared<FNiagaraHierarchyDragDropOp>(nullptr);
}

void UNiagaraHierarchyScriptParametersViewModel::FinalizeInternal()
{
	if(ScriptViewModelWeak.IsValid())
	{
		// If this is called during Undo, it's possible the Graph does no longer exist
		if(UNiagaraGraph* Graph = Cast<UNiagaraScriptSource>(ScriptViewModelWeak.Pin()->GetStandaloneScript().GetScriptData()->GetSource())->NodeGraph)
		{
			Graph->OnParametersChanged().RemoveAll(this);
		}
	}

	Super::FinalizeInternal();
}

void UNiagaraHierarchyScriptParametersViewModel::OnParametersChanged(TOptional<UNiagaraGraph::FParametersChangedData> ParametersChangedData)
{
	ForceFullRefresh();
}

TSharedRef<SWidget> FNiagaraHierarchyScriptParameterDragDropOp::CreateCustomDecorator() const
{
	if(const UNiagaraHierarchyScriptParameter* ScriptParameter = Cast<UNiagaraHierarchyScriptParameter>(DraggedItem.Pin()->GetData()))
	{
		return FNiagaraParameterUtilities::GetParameterWidget(ScriptParameter->GetVariable(), false, false);
	}

	return SNullWidget::NullWidget;
}

bool FNiagaraHierarchyScriptParameterViewModel::DoesExternalDataStillExist(const UNiagaraHierarchyDataRefreshContext* Context) const
{
	// During undo/redo it's possible the script variable becomes nullptr. If so, there is no need for this view model either
	if(Cast<UNiagaraHierarchyScriptParameter>(GetDataMutable())->GetScriptVariable() == nullptr)
	{
		return false;
	}
	
	const UNiagaraHierarchyScriptParameterRefreshContext* RefreshContext = CastChecked<UNiagaraHierarchyScriptParameterRefreshContext>(Context);
	if(RefreshContext->GetNiagaraGraph()->GetAllMetaData().Contains(Cast<UNiagaraHierarchyScriptParameter>(GetDataMutable())->GetVariable()) == false)
	{
		return false;
	}

	const UNiagaraScriptVariable* ScriptVariable = Cast<UNiagaraHierarchyScriptParameter>(GetDataMutable())->GetScriptVariable();

	// We make sure that the variable not only still exists but also qualifies for the hierarchy (namespace can change for example)
	if(ScriptVariable->GetIsStaticSwitch() == false && ScriptVariable->Variable.IsInNameSpace(FNiagaraConstants::ModuleNamespace) == false)
	{
		return false;
	}
	
	return true;
}

FNiagaraHierarchyItemViewModelBase::FCanPerformActionResults FNiagaraHierarchyScriptParameterViewModel::CanDropOnInternal(TSharedPtr<FNiagaraHierarchyItemViewModelBase> DraggedItem, EItemDropZone ItemDropZone)
{
	// if the input isn't editable, we don't allow any drops on/above/below the item.
	// Even though it technically works, the merge process will only re-add the item at the end and not preserve order so there is no point in allowing dropping above/below
	if(IsEditableByUser().bCanPerform == false)
	{
		return false;
	}

	FCanPerformActionResults AllowDrop(false);
	
	TSharedPtr<FNiagaraHierarchyItemViewModelBase> TargetDropItem = AsShared();

	// we only allow drops if some general conditions are fulfilled
	if(DraggedItem->GetData() != TargetDropItem->GetData() &&
		(!DraggedItem->HasParent(TargetDropItem, false) || ItemDropZone != EItemDropZone::OntoItem)  &&
		!TargetDropItem->HasParent(DraggedItem, true))
	{
		if(ItemDropZone == EItemDropZone::OntoItem)
		{
			// We support nested inputs
			if(DraggedItem->GetData()->IsA<UNiagaraHierarchyScriptParameter>() && TargetDropItem->GetData()->IsA<UNiagaraHierarchyScriptParameter>())
			{
				FText BaseMessage = LOCTEXT("DroppingInputOnInputNestedChild", "This will nest input {0} under input {1}");
				AllowDrop.CanPerformMessage = FText::FormatOrdered(BaseMessage, DraggedItem->ToStringAsText(), TargetDropItem->ToStringAsText());
				AllowDrop.bCanPerform = true;
			}
		}
		else
		{
			// if the dragged item is an input, we generally allow above/below, even for nested child inputs
			if(DraggedItem->GetData()->IsA<UNiagaraHierarchyScriptParameter>())
			{
				AllowDrop.bCanPerform = true;
			}
			else
			{
				// we use default logic only if there is no parent input. Nested children are not allowed to contain anything but other inputs.
				if(TargetDropItem->GetParent().Pin()->GetData<UNiagaraHierarchyScriptParameter>() == nullptr)
				{
					AllowDrop = FNiagaraHierarchyItemViewModel::CanDropOnInternal(DraggedItem, ItemDropZone);
				}
			}
		}
	}

	return AllowDrop;
}

void FNiagaraHierarchyScriptParameterViewModel::OnDroppedOnInternal(TSharedPtr<FNiagaraHierarchyItemViewModelBase> DroppedItem, EItemDropZone ItemDropZone)
{
	if(ItemDropZone != EItemDropZone::OntoItem)
	{
		return FNiagaraHierarchyItemViewModel::OnDroppedOnInternal(DroppedItem, ItemDropZone);
	}
	else
	{
		FScopedTransaction Transaction(LOCTEXT("Transaction_AddedChildInput", "Added child input"));
		HierarchyViewModel->GetHierarchyRoot()->Modify();
		
		if(DroppedItem->IsForHierarchy() == false)
		{
			TSharedRef<FNiagaraHierarchyItemViewModelBase> AddedItemViewModel = DuplicateToThis(DroppedItem);
			AddedItemViewModel->SyncViewModelsToData();
		}
		else
		{
			TSharedRef<FNiagaraHierarchyItemViewModelBase> ReparentedViewModel = ReparentToThis(DroppedItem);
			ReparentedViewModel->SyncViewModelsToData();
		}

		HierarchyViewModel->RefreshHierarchyView();
		HierarchyViewModel->RefreshSourceView();
	}
}

void FNiagaraHierarchyScriptRootViewModel::SortChildrenData() const
{
	GetDataMutable()->GetChildrenMutable().StableSort([](const UNiagaraHierarchyItemBase& ItemA, const UNiagaraHierarchyItemBase& ItemB)
		{
			return ItemA.IsA<UNiagaraHierarchyItem>() && ItemB.IsA<UNiagaraHierarchyCategory>();
		});
}

#undef LOCTEXT_NAMESPACE
