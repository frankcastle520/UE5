// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeEditingSubsystem.h"

#include "SStateTreeView.h"
#include "StateTreeCompiler.h"
#include "StateTreeCompilerLog.h"
#include "StateTreeDelegates.h"
#include "StateTreeEditorData.h"
#include "StateTreeEditorModule.h"
#include "StateTreeObjectHash.h"
#include "StateTreeTaskBase.h"
#include "UObject/UObjectGlobals.h"


UStateTreeEditingSubsystem::UStateTreeEditingSubsystem()
{
	PostGarbageCollectHandle = FCoreUObjectDelegates::GetPostGarbageCollect().AddUObject(this, &UStateTreeEditingSubsystem::HandlePostGarbageCollect);
}

void UStateTreeEditingSubsystem::BeginDestroy()
{
	FCoreUObjectDelegates::GetPostGarbageCollect().Remove(PostGarbageCollectHandle);
	Super::BeginDestroy();
}

bool UStateTreeEditingSubsystem::CompileStateTree(const gsl::not_null<UStateTree*> InStateTree, FStateTreeCompilerLog& InOutLog)
{
	ValidateStateTree(InStateTree);
	const uint32 EditorDataHash = CalculateStateTreeHash(InStateTree);
	
	FStateTreeCompiler Compiler(InOutLog);

	const bool bCompilationResult = Compiler.Compile(*InStateTree);
	if(bCompilationResult)
	{
		// Success
		InStateTree->LastCompiledEditorDataHash = EditorDataHash;
		UE::StateTree::Delegates::OnPostCompile.Broadcast(*InStateTree);
		UE_LOG(LogStateTreeEditor, Log, TEXT("Compile StateTree '%s' succeeded."), *InStateTree->GetFullName());
	}
	else
	{
		// Make sure not to leave stale data on failed compile.
		InStateTree->ResetCompiled();
		InStateTree->LastCompiledEditorDataHash = 0;
		
		UE_LOG(LogStateTreeEditor, Error, TEXT("Failed to compile '%s', errors follow."), *InStateTree->GetFullName());
		InOutLog.DumpToLog(LogStateTreeEditor);
	}	

	return bCompilationResult;
}

TSharedRef<FStateTreeViewModel> UStateTreeEditingSubsystem::FindOrAddViewModel(const gsl::not_null<UStateTree*> InStateTree)
{
	const FObjectKey StateTreeKey = InStateTree;
	TSharedPtr<FStateTreeViewModel> ViewModelPtr = StateTreeViewModels.FindRef(StateTreeKey);
	if (ViewModelPtr)
	{
		// The StateTree could be re-instantiated. Can occur when the object is destroyed and recreated in a pool or when reloaded in editor.
		//The object might have the same pointer value or the same path but it's a new object and all weakptr are now invalid.
		if (ViewModelPtr->GetStateTree() == InStateTree)
		{
			return ViewModelPtr.ToSharedRef();
		}
		else
		{
			StateTreeViewModels.Remove(StateTreeKey);
			ViewModelPtr = nullptr;
		}
	}

	TSharedRef<FStateTreeViewModel> SharedModel = StateTreeViewModels.Add(StateTreeKey, MakeShared<FStateTreeViewModel>()).ToSharedRef();
	UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(InStateTree->EditorData);
	if (EditorData == nullptr)
	{
		EditorData = NewObject<UStateTreeEditorData>(InStateTree, FName(), RF_Transactional);
		EditorData->AddRootState();
		InStateTree->EditorData = EditorData;

		FStateTreeCompilerLog Log;
		CompileStateTree(InStateTree, Log);
	}
	
	for (UStateTreeState* SubTree : EditorData->SubTrees)
	{
		TArray<UStateTreeState*> Stack;

		Stack.Add(SubTree);
		while (!Stack.IsEmpty())
		{
			if (UStateTreeState* State = Stack.Pop())
			{
				State->SetFlags(RF_Transactional);
				
				for (UStateTreeState* ChildState : State->Children)
				{
					Stack.Add(ChildState);
				}
			}
		}
	}
		
	SharedModel->Init(EditorData);

	return SharedModel;
}

TSharedRef<SWidget> UStateTreeEditingSubsystem::GetStateTreeView(TSharedRef<FStateTreeViewModel> InViewModel, const TSharedRef<FUICommandList>& TreeViewCommandList)
{
	return SNew(SStateTreeView, InViewModel, TreeViewCommandList);
}

void UStateTreeEditingSubsystem::ValidateStateTree(const gsl::not_null<UStateTree*> InStateTree)
{
	auto FixChangedStateLinkName = [](FStateTreeStateLink& StateLink, const TMap<FGuid, FName>& IDToName) -> bool
	{
		if (StateLink.ID.IsValid())
		{
			const FName* Name = IDToName.Find(StateLink.ID);
			if (Name == nullptr)
			{
				// Missing link, we'll show these in the UI
				return false;
			}
			if (StateLink.Name != *Name)
			{
				// Name changed, fix!
				StateLink.Name = *Name;
				return true;
			}
		}
		return false;
	};

	auto ValidateLinkedStates = [FixChangedStateLinkName](const UStateTree& StateTree)
	{
		UStateTreeEditorData* TreeData = Cast<UStateTreeEditorData>(StateTree.EditorData);
		if (!TreeData)
		{
			return;
		}

		constexpr bool bMarkDirty = false;
		TreeData->Modify(bMarkDirty);

		// Make sure all state links are valid and update the names if needed.

		// Create ID to state name map.
		TMap<FGuid, FName> IDToName;

		TreeData->VisitHierarchy([&IDToName](const UStateTreeState& State, UStateTreeState* /*ParentState*/)
		{
			IDToName.Add(State.ID, State.Name);
			return EStateTreeVisitor::Continue;
		});
		
		// Fix changed names.
		TreeData->VisitHierarchy([&IDToName, FixChangedStateLinkName](UStateTreeState& State, UStateTreeState* /*ParentState*/)
		{
			constexpr bool bMarkDirty = false;
			State.Modify(bMarkDirty);
			if (State.Type == EStateTreeStateType::Linked)
			{
				FixChangedStateLinkName(State.LinkedSubtree, IDToName);
			}
					
			for (FStateTreeTransition& Transition : State.Transitions)
			{
				FixChangedStateLinkName(Transition.State, IDToName);
			}

			return EStateTreeVisitor::Continue;
		});
	};

	auto UpdateParents = [](const UStateTree& StateTree)
	{
		UStateTreeEditorData* TreeData = Cast<UStateTreeEditorData>(StateTree.EditorData);
		if (!TreeData)
		{
			return;
		}

		constexpr bool bMarkDirty = false;
		TreeData->Modify(bMarkDirty);
		TreeData->ReparentStates();
	};

	auto ApplySchema =[](const UStateTree& StateTree)
	{
		UStateTreeEditorData* TreeData = Cast<UStateTreeEditorData>(StateTree.EditorData);
		if (!TreeData)
		{
			return;
		
		}
		const UStateTreeSchema* Schema = TreeData->Schema;
		if (!Schema)
		{
			return;
		}

		constexpr bool bMarkDirty = false;
		TreeData->Modify(bMarkDirty);
		
		// Clear evaluators if not allowed.
		if (Schema->AllowEvaluators() == false && TreeData->Evaluators.Num() > 0)
		{
			UE_LOG(LogStateTreeEditor, Warning, TEXT("%s: Resetting Evaluators due to current schema restrictions."), *GetNameSafe(&StateTree));
			TreeData->Evaluators.Reset();
		}


		TreeData->VisitHierarchy([&StateTree, Schema](UStateTreeState& State, UStateTreeState* /*ParentState*/)
		{
			constexpr bool bMarkDirty = false;
			State.Modify(bMarkDirty);

			// Clear enter conditions if not allowed.
			if (Schema->AllowEnterConditions() == false && State.EnterConditions.Num() > 0)
			{
				UE_LOG(LogStateTreeEditor, Warning, TEXT("%s: Resetting Enter Conditions in state %s due to current schema restrictions."), *GetNameSafe(&StateTree), *GetNameSafe(&State));
				State.EnterConditions.Reset();
			}

			// Clear Utility if not allowed
			if (Schema->AllowUtilityConsiderations() == false && State.Considerations.Num() > 0)
			{
				UE_LOG(LogStateTreeEditor, Warning, TEXT("%s: Resetting Utility Considerations in state %s due to current schema restrictions."), *GetNameSafe(&StateTree), *GetNameSafe(&State));
				State.Considerations.Reset();
			}

			// Keep single and many tasks based on what is allowed.
			if (Schema->AllowMultipleTasks() == false)
			{
				if (State.Tasks.Num() > 0)
				{
					State.Tasks.Reset();
					UE_LOG(LogStateTreeEditor, Warning, TEXT("%s: Resetting Tasks in state %s due to current schema restrictions."), *GetNameSafe(&StateTree), *GetNameSafe(&State));
				}
				
				// Task name is the same as state name.
				if (FStateTreeTaskBase* Task = State.SingleTask.Node.GetMutablePtr<FStateTreeTaskBase>())
				{
					Task->Name = State.Name;
				}
			}
			else
			{
				if (State.SingleTask.Node.IsValid())
				{
					State.SingleTask.Reset();
					UE_LOG(LogStateTreeEditor, Warning, TEXT("%s: Resetting Single Task in state %s due to current schema restrictions."), *GetNameSafe(&StateTree), *GetNameSafe(&State));
				}
			}
			
			return EStateTreeVisitor::Continue;
		});
	};
	

	auto RemoveUnusedBindings = [](const UStateTree& StateTree)
	{
		UStateTreeEditorData* TreeData = Cast<UStateTreeEditorData>(StateTree.EditorData);
		if (!TreeData)
		{
			return;
		}

		TMap<FGuid, const FStateTreeDataView> AllStructValues;
		TreeData->GetAllStructValues(AllStructValues);
		constexpr bool bMarkDirty = false;
		TreeData->Modify(bMarkDirty);
		TreeData->GetPropertyEditorBindings()->RemoveUnusedBindings(AllStructValues);
	};

	auto UpdateLinkedStateParameters = [](const UStateTree& StateTree)
	{
		UStateTreeEditorData* TreeData = Cast<UStateTreeEditorData>(StateTree.EditorData);
		if (!TreeData)
		{
			return;
		}

		constexpr bool bMarkDirty = false;
		TreeData->Modify(bMarkDirty);

		const EStateTreeVisitor Result = TreeData->VisitHierarchy([](UStateTreeState& State, UStateTreeState* /*ParentState*/)
		{
			if (State.Type == EStateTreeStateType::Linked
				|| State.Type == EStateTreeStateType::LinkedAsset)
			{
				State.Modify();
				State.UpdateParametersFromLinkedSubtree();
			}
			return EStateTreeVisitor::Continue;
		});
	};


	UpdateParents(*InStateTree);
	ApplySchema(*InStateTree);
	RemoveUnusedBindings(*InStateTree);
	ValidateLinkedStates(*InStateTree);
	UpdateLinkedStateParameters(*InStateTree);
}

uint32 UStateTreeEditingSubsystem::CalculateStateTreeHash(const gsl::not_null<const UStateTree*> InStateTree)
{
	uint32 EditorDataHash = 0;
	if (InStateTree->EditorData != nullptr)
	{
		FStateTreeObjectCRC32 Archive;
		EditorDataHash = Archive.Crc32(InStateTree->EditorData, 0);
	}

	return EditorDataHash;
}

void UStateTreeEditingSubsystem::HandlePostGarbageCollect()
{
	// Remove the stale viewmodels
	for (TMap<FObjectKey, TSharedPtr<FStateTreeViewModel>>::TIterator It(StateTreeViewModels); It; ++It)
	{
		if (!It.Key().ResolveObjectPtr())
		{
			It.RemoveCurrent();
		}
		else if (!It.Value() || !It.Value()->GetStateTree())
		{
			It.RemoveCurrent();
		}
	}
}
