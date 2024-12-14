// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeCompiler.h"
#include "StateTree.h"
#include "StateTreeAnyEnum.h"
#include "StateTreeCompilerLog.h"
#include "StateTreeEditorData.h"
#include "StateTreeEditorModule.h"
#include "StateTreeEvaluatorBase.h"
#include "StateTreeTaskBase.h"
#include "StateTreeConditionBase.h"
#include "StateTreeConsiderationBase.h"
#include "Serialization/ArchiveUObject.h"
#include "GameFramework/Actor.h"
#include "StateTreePropertyRef.h"
#include "StateTreePropertyRefHelpers.h"
#include "StateTreePropertyHelpers.h"
#include "StateTreePropertyFunctionBase.h"


namespace UE::StateTree::Compiler
{
	FAutoConsoleVariable CVarLogCompiledStateTree(
		TEXT("StateTree.Compiler.LogResultOnCompilationCompleted"),
		false,
		TEXT("After a StateTree compiles, log the internal content of the StateTree.")
	);

	// Helper archive that checks that the all instanced sub-objects have correct outer. 
	class FCheckOutersArchive : public FArchiveUObject
	{
		using Super = FArchiveUObject;
		const UStateTree& StateTree;
		const UStateTreeEditorData& EditorData;
		FStateTreeCompilerLog& Log;
	public:

		FCheckOutersArchive(const UStateTree& InStateTree, const UStateTreeEditorData& InEditorData, FStateTreeCompilerLog& InLog)
			: StateTree(InStateTree)
			, EditorData(InEditorData)
			, Log(InLog)
		{
			Super::SetIsSaving(true);
			Super::SetIsPersistent(true);
		}

		virtual bool ShouldSkipProperty(const FProperty* InProperty) const
		{
			// Skip editor data.
			if (const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(InProperty))
			{
				if (ObjectProperty->PropertyClass == UStateTreeEditorData::StaticClass())
				{
					return true;
				}
			}
			return false;
		}

		virtual FArchive& operator<<(UObject*& Object) override
		{
			if (Object)
			{
				if (const FProperty* Property = GetSerializedProperty())
				{
					if (Property->HasAnyPropertyFlags(CPF_InstancedReference))
					{
						if (!Object->IsInOuter(&StateTree))
						{
							Log.Reportf(EMessageSeverity::Error, TEXT("Compiled StateTree contains instanced object %s (%s), which does not belong to the StateTree. This is due to error in the State Tree node implementation."),
								*GetFullNameSafe(Object), *GetFullNameSafe(Object->GetClass()));
						}

						if (Object->IsInOuter(&EditorData))
						{
							Log.Reportf(EMessageSeverity::Error, TEXT("Compiled StateTree contains instanced object %s (%s), which still belongs to the Editor data. This is due to error in the State Tree node implementation."),
								*GetFullNameSafe(Object), *GetFullNameSafe(Object->GetClass()));
						}
					}
				}
			}
			return *this;
		}
	};

	/** Scans Data for actors that are tied to some level and returns them. */
	void ScanLevelActorReferences(FStateTreeDataView Data, TSet<const UObject*>& Visited, TArray<const AActor*>& OutActors)
	{
		if (!Data.IsValid())
		{
			return;
		}
		
		for (TPropertyValueIterator<FProperty> It(Data.GetStruct(), Data.GetMemory()); It; ++It)
		{
			const FProperty* Property = It->Key;
			const void* ValuePtr = It->Value;

			if (!ValuePtr)
			{
				continue;
			}
			
			if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				if (StructProperty->Struct == TBaseStructure<FInstancedStruct>::Get())
				{
					const FInstancedStruct& InstancedStruct = *static_cast<const FInstancedStruct*>(ValuePtr);
					if (InstancedStruct.IsValid())
					{
						ScanLevelActorReferences(FStateTreeDataView(const_cast<FInstancedStruct&>(InstancedStruct)), Visited, OutActors);
					}
				}
			}
			else if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
			{
				if (const UObject* Object = ObjectProperty->GetObjectPropertyValue(ValuePtr))
				{
					if (const AActor* Actor = Cast<AActor>(Object))
					{
						const ULevel* Level = Actor->GetLevel();
						if (Level != nullptr)
						{
							OutActors.Add(Actor);
						}
					}
					// Recurse into instanced object
					if (Property->HasAnyPropertyFlags(CPF_InstancedReference))
					{
						if (!Visited.Contains(Object))
						{
							Visited.Add(Object);
							ScanLevelActorReferences(FStateTreeDataView(const_cast<UObject*>(Object)), Visited, OutActors);
						}
					}
				}
			}
		}
	}

	bool ValidateNoLevelActorReferences(FStateTreeCompilerLog& Log, const FStateTreeBindableStructDesc& NodeDesc, FStateTreeDataView NodeView, FStateTreeDataView InstanceView)
	{
		TSet<const UObject*> Visited;
		TArray<const AActor*> LevelActors;
		UE::StateTree::Compiler::ScanLevelActorReferences(NodeView, Visited, LevelActors);
		UE::StateTree::Compiler::ScanLevelActorReferences(InstanceView, Visited, LevelActors);
		if (!LevelActors.IsEmpty())
		{
			FStringBuilderBase AllActorsString;
			for (const AActor* Actor : LevelActors)
			{
				if (AllActorsString.Len() > 0)
				{
					AllActorsString += TEXT(", ");
				}
				AllActorsString += *GetNameSafe(Actor);
			}
			Log.Reportf(EMessageSeverity::Error, NodeDesc,
				TEXT("Level Actor references were found: %s. Direct Actor references are not allowed."),
					*AllActorsString);
			return false;
		}
		
		return true;
	}


	void FValidationResult::Log(FStateTreeCompilerLog& Log, const TCHAR* ContextText, const FStateTreeBindableStructDesc& ContextStruct) const
	{
		Log.Reportf(EMessageSeverity::Error, ContextStruct, TEXT("The StateTree is too complex. Compact index %s out of range %d/%d."), ContextText, Value, MaxValue);
	}

	const UScriptStruct* GetBaseStructFromMetaData(const FProperty* Property, FString& OutBaseStructName)
	{
		static const FName NAME_BaseStruct = "BaseStruct";

		const UScriptStruct* Result = nullptr;
		OutBaseStructName = Property->GetMetaData(NAME_BaseStruct);
	
		if (!OutBaseStructName.IsEmpty())
		{
			Result = UClass::TryFindTypeSlow<UScriptStruct>(OutBaseStructName);
			if (!Result)
			{
				Result = LoadObject<UScriptStruct>(nullptr, *OutBaseStructName);
			}
		}

		return Result;
	}

}; // UE::StateTree::Compiler

bool FStateTreeCompiler::Compile(UStateTree& InStateTree)
{
	StateTree = &InStateTree;
	EditorData = Cast<UStateTreeEditorData>(StateTree->EditorData);
	if (!EditorData)
	{
		return false;
	}
	
	// Cleanup existing state
	StateTree->ResetCompiled();

	if (!EditorData->Schema)
	{
		Log.Reportf(EMessageSeverity::Error, TEXT("Missing Schema. Please set valid schema in the State Tree Asset settings."));
		return false;
	}
	Schema = EditorData->Schema;

	if (!BindingsCompiler.Init(StateTree->PropertyBindings, Log))
	{
		StateTree->ResetCompiled();
		return false;
	}

	EditorData->GetAllStructValues(IDToStructValue);

	// Copy schema the EditorData
	StateTree->Schema = DuplicateObject(EditorData->Schema, StateTree);

	// Copy parameters from EditorData	
	StateTree->Parameters = EditorData->RootParameters.Parameters;
	
	// Mark parameters as binding source
	const FStateTreeBindableStructDesc ParametersDesc = {
			UE::StateTree::Editor::GlobalStateName,
			TEXT("Parameters"),
			StateTree->Parameters.GetPropertyBagStruct(),
			FStateTreeDataHandle(EStateTreeDataSourceType::GlobalParameterData),
			EStateTreeBindableStructSource::Parameter,
			EditorData->RootParameters.ID
		};
	BindingsCompiler.AddSourceStruct(ParametersDesc);

	if (!UE::StateTree::Compiler::ValidateNoLevelActorReferences(Log, ParametersDesc, FStateTreeDataView(), FStateTreeDataView(EditorData->RootParameters.Parameters.GetMutableValue())))
	{
		StateTree->ResetCompiled();
		return false;
	}

	int32 ContextDataIndex = 0;

	// Mark all named external values as binding source
	if (StateTree->Schema)
	{
		StateTree->ContextDataDescs = StateTree->Schema->GetContextDataDescs();
		for (FStateTreeExternalDataDesc& Desc : StateTree->ContextDataDescs)
		{
			const FStateTreeBindableStructDesc ExtDataDesc = {
					UE::StateTree::Editor::GlobalStateName,
					Desc.Name,
					Desc.Struct,
					FStateTreeDataHandle(EStateTreeDataSourceType::ContextData, ContextDataIndex++),
					EStateTreeBindableStructSource::Context,
					Desc.ID
				};
			BindingsCompiler.AddSourceStruct(ExtDataDesc);
			if (const auto Validation = UE::StateTree::Compiler::IsValidIndex16(ContextDataIndex); Validation.DidFail())
			{
				Validation.Log(Log, TEXT("ExternalStructIndex"), ParametersDesc);
				return false;
			}
			Desc.Handle.DataHandle = ExtDataDesc.DataHandle;
		} 
	}

	if (const UE::StateTree::Compiler::FValidationResult Validation = UE::StateTree::Compiler::IsValidIndex16(ContextDataIndex); Validation.DidFail())
	{
		Validation.Log(Log, TEXT("NumContextData"), ParametersDesc);
		return false;
	}
	StateTree->NumContextData = static_cast<uint16>(ContextDataIndex);
	
	if (!CreateStates())
	{
		StateTree->ResetCompiled();
		return false;
	}

	// Eval and Global task methods use InstanceStructs.Num() as ID generator.
	check(InstanceStructs.Num() == 0);
	
	if (!CreateEvaluators())
	{
		StateTree->ResetCompiled();
		return false;
	}

	if (!CreateGlobalTasks())
	{
		StateTree->ResetCompiled();
		return false;
	}

	const int32 NumGlobalInstanceData = InstanceStructs.Num();
	if (const auto Validation = UE::StateTree::Compiler::IsValidIndex16(NumGlobalInstanceData); Validation.DidFail())
	{
		Validation.Log(Log, TEXT("NumGlobalInstanceData"), ParametersDesc);
		return false;
	}
	StateTree->NumGlobalInstanceData = uint16(NumGlobalInstanceData);

	if (!CreateStateTasksAndParameters())
	{
		StateTree->ResetCompiled();
		return false;
	}

	if (!CreateStateTransitions())
	{
		StateTree->ResetCompiled();
		return false;
	}

	if (!CreateStateConsiderations())
	{
		StateTree->ResetCompiled();
		return false;
	}

	StateTree->Nodes = Nodes;
	StateTree->DefaultInstanceData.Init(*StateTree, InstanceStructs);
	StateTree->SharedInstanceData.Init(*StateTree, SharedInstanceStructs);
	
	BindingsCompiler.Finalize();

	if (!StateTree->Link())
	{
		StateTree->ResetCompiled();
		Log.Reportf(EMessageSeverity::Error, TEXT("Unexpected failure to link the StateTree asset. See log for more info."));
		return false;
	}

	// Store mapping between node unique ID and their compiled index. Used for debugging purposes.
	for (const TPair<FGuid, int32>& ToNode : IDToNode)
	{
		StateTree->IDToNodeMappings.Emplace(ToNode.Key, FStateTreeIndex16(ToNode.Value));
	}

	// Store mapping between state unique ID and state handle. Used for debugging purposes.
	for (const TPair<FGuid, int32>& ToState : IDToState)
	{
		StateTree->IDToStateMappings.Emplace(ToState.Key, FStateTreeStateHandle(ToState.Value));
	}

	// Store mapping between state transition identifier and compact transition index. Used for debugging purposes.
	for (const TPair<FGuid, int32>& ToTransition: IDToTransition)
	{
		StateTree->IDToTransitionMappings.Emplace(ToTransition.Key, FStateTreeIndex16(ToTransition.Value));
	}

	UE::StateTree::Compiler::FCheckOutersArchive CheckOuters(*StateTree, *EditorData, Log);
	StateTree->Serialize(CheckOuters);

	if (UE::StateTree::Compiler::CVarLogCompiledStateTree->GetBool())
	{
		UE_LOG(LogStateTreeEditor, Log, TEXT("%s"), *StateTree->DebugInternalLayoutAsString());
	}

	return true;
}

FStateTreeStateHandle FStateTreeCompiler::GetStateHandle(const FGuid& StateID) const
{
	const int32* Idx = IDToState.Find(StateID);
	if (Idx == nullptr)
	{
		return FStateTreeStateHandle::Invalid;
	}

	return FStateTreeStateHandle(uint16(*Idx));
}

UStateTreeState* FStateTreeCompiler::GetState(const FGuid& StateID) const
{
	const int32* Idx = IDToState.Find(StateID);
	if (Idx == nullptr)
	{
		return nullptr;
	}

	return SourceStates[*Idx];
}

bool FStateTreeCompiler::CreateStates()
{
	check(EditorData);
	
	// Create main tree (omit subtrees)
	for (UStateTreeState* SubTree : EditorData->SubTrees)
	{
		if (SubTree != nullptr
			&& SubTree->Type != EStateTreeStateType::Subtree)
		{
			if (!CreateStateRecursive(*SubTree, FStateTreeStateHandle::Invalid))
			{
				return false;
			}
		}
	}

	// Create Subtrees
	for (UStateTreeState* SubTree : EditorData->SubTrees)
	{
		TArray<UStateTreeState*> Stack;
		Stack.Push(SubTree);
		while (!Stack.IsEmpty())
		{
			if (UStateTreeState* State = Stack.Pop())
			{
				if (State->Type == EStateTreeStateType::Subtree)
				{
					if (!CreateStateRecursive(*State, FStateTreeStateHandle::Invalid))
					{
						return false;
					}
				}
				Stack.Append(State->Children);
			}
		}
	}

	return true;
}

bool FStateTreeCompiler::CreateStateRecursive(UStateTreeState& State, const FStateTreeStateHandle Parent)
{
	check(StateTree);

	FStateTreeCompilerLogStateScope LogStateScope(&State, Log);

	if ((State.Type == EStateTreeStateType::LinkedAsset
		|| State.Type == EStateTreeStateType::Linked)
		&& State.Children.Num() > 0)
	{
		Log.Reportf(EMessageSeverity::Warning,
			TEXT("Linked State cannot have child states, because the state selection will enter to the linked state on activation."));
	}

	const int32 StateIdx = StateTree->States.AddDefaulted();
	FCompactStateTreeState& CompactState = StateTree->States[StateIdx];
	CompactState.Name = State.Name;
	CompactState.Tag = State.Tag;
	CompactState.Parent = Parent;
	CompactState.bEnabled = State.bEnabled;
	CompactState.bCheckPrerequisitesWhenActivatingChildDirectly = State.bCheckPrerequisitesWhenActivatingChildDirectly;
	CompactState.Weight = State.Weight;

	CompactState.Type = State.Type;
	CompactState.SelectionBehavior = State.SelectionBehavior;

	SourceStates.Add(&State);
	IDToState.Add(State.ID, StateIdx);

	// Child states
	const int32 ChildrenBegin = StateTree->States.Num();
	if (const auto Validation = UE::StateTree::Compiler::IsValidCount16(ChildrenBegin); Validation.DidFail())
	{
		Validation.Log(Log, TEXT("ChildrenBegin"));
		return false;
	}
	CompactState.ChildrenBegin = uint16(ChildrenBegin);

	for (UStateTreeState* Child : State.Children)
	{
		if (Child != nullptr && Child->Type != EStateTreeStateType::Subtree)
		{
			if (!CreateStateRecursive(*Child, FStateTreeStateHandle((uint16)StateIdx)))
			{
				return false;
			}
		}
	}
	
	const int32 ChildrenEnd = StateTree->States.Num();
	if (const auto Validation = UE::StateTree::Compiler::IsValidCount16(ChildrenEnd); Validation.DidFail())
	{
		Validation.Log(Log, TEXT("ChildrenEnd"));
		return false;
	}
	StateTree->States[StateIdx].ChildrenEnd = uint16(ChildrenEnd); // Not using CompactState here because the array may have changed.
	
	return true;
}

bool FStateTreeCompiler::CreateConditions(UStateTreeState& State, const FString& StatePath, TConstArrayView<FStateTreeEditorNode> Conditions)
{
	bool bSucceeded = true;

	for (int32 Index = 0; Index < Conditions.Num(); Index++)
	{
		const bool bIsFirst = Index == 0;
		const FStateTreeEditorNode& CondNode = Conditions[Index];
		// First operand should be copy as we dont have a previous item to operate on.
		const EStateTreeExpressionOperand Operand = bIsFirst ? EStateTreeExpressionOperand::Copy : CondNode.ExpressionOperand;
		// First indent must be 0 to make the parentheses calculation match.
		const int32 CurrIndent = bIsFirst ? 0 : FMath::Clamp((int32)CondNode.ExpressionIndent, 0, UE::StateTree::MaxExpressionIndent);
		// Next indent, or terminate at zero.
		const int32 NextIndent = Conditions.IsValidIndex(Index + 1) ? FMath::Clamp((int32)Conditions[Index + 1].ExpressionIndent, 0, UE::StateTree::MaxExpressionIndent) : 0;
		
		const int32 DeltaIndent = NextIndent - CurrIndent;

		if (!CreateCondition(State, StatePath, CondNode, Operand, (int8)DeltaIndent))
		{
			bSucceeded = false;
			continue;
		}
	}

	return bSucceeded;
}

bool FStateTreeCompiler::CreateEvaluators()
{
	check(EditorData);
	check(StateTree);

	bool bSucceeded = true;

	const int32 EvaluatorsBegin = Nodes.Num();
	if (const auto Validation = UE::StateTree::Compiler::IsValidCount16(EvaluatorsBegin); Validation.DidFail())
	{
		Validation.Log(Log, TEXT("EvaluatorsBegin"));
		return false;
	}
	StateTree->EvaluatorsBegin = uint16(EvaluatorsBegin);

	for (FStateTreeEditorNode& EvalNode : EditorData->Evaluators)
	{
		const int32 GlobalInstanceIndex = InstanceStructs.Num();
		const FStateTreeDataHandle EvalDataHandle(EStateTreeDataSourceType::GlobalInstanceData, GlobalInstanceIndex);
		if (!CreateEvaluator(EvalNode, EvalDataHandle))
		{
			bSucceeded = false;
			continue;
		}
	}
	
	const int32 EvaluatorsNum = Nodes.Num() - EvaluatorsBegin;
	if (const auto Validation = UE::StateTree::Compiler::IsValidCount16(EvaluatorsNum); Validation.DidFail())
	{
		Validation.Log(Log, TEXT("EvaluatorsNum"));
		return false;
	}
	StateTree->EvaluatorsNum = uint16(EvaluatorsNum);

	return bSucceeded && CreateBindingsForNodes(EditorData->Evaluators, FStateTreeIndex16(EvaluatorsBegin), InstanceStructs);
}

bool FStateTreeCompiler::CreateGlobalTasks()
{
	check(EditorData);
	check(StateTree);

	bool bSucceeded = true;

	const int32 GlobalTasksBegin = Nodes.Num();
	if (const auto Validation = UE::StateTree::Compiler::IsValidCount16(GlobalTasksBegin); Validation.DidFail())
	{
		Validation.Log(Log, TEXT("GlobalTasksBegin"));
		return false;
	}
	StateTree->GlobalTasksBegin = uint16(GlobalTasksBegin);

	StateTree->bHasGlobalTransitionTasks = false;
	for (FStateTreeEditorNode& TaskNode : EditorData->GlobalTasks)
	{
		// Silently ignore empty nodes.
		if (!TaskNode.Node.IsValid())
		{
			continue;
		}

		const int32 GlobalInstanceIndex = InstanceStructs.Num();
		const FStateTreeDataHandle TaskDataHandle(EStateTreeDataSourceType::GlobalInstanceData, GlobalInstanceIndex);
		if (!CreateTask(nullptr, TaskNode, TaskDataHandle))
		{
			bSucceeded = false;
			continue;
		}
		
		const FStateTreeTaskBase& LastAddedTask = Nodes.Last().Get<FStateTreeTaskBase>();
		
		StateTree->bHasGlobalTransitionTasks |= LastAddedTask.bShouldAffectTransitions;
	}
	
	const int32 GlobalTasksNum = Nodes.Num() - GlobalTasksBegin;
	if (const auto Validation = UE::StateTree::Compiler::IsValidCount16(GlobalTasksNum); Validation.DidFail())
	{
		Validation.Log(Log, TEXT("GlobalTasksNum"));
		return false;
	}
	StateTree->GlobalTasksNum = uint16(GlobalTasksNum);

	return bSucceeded && CreateBindingsForNodes(EditorData->GlobalTasks, FStateTreeIndex16(GlobalTasksBegin), InstanceStructs);
}

bool FStateTreeCompiler::CreateStateTasksAndParameters()
{
	check(StateTree);

	bool bSucceeded = true;

	// Index of the first instance data per state. Accumulated depth first.
	TArray<int32> FirstInstanceDataIndex;
	FirstInstanceDataIndex.Init(0, StateTree->States.Num());
	
	for (int32 i = 0; i < StateTree->States.Num(); i++)
	{
		FCompactStateTreeState& CompactState = StateTree->States[i];
		const FStateTreeStateHandle CompactStateHandle(i);
		UStateTreeState* State = SourceStates[i];
		check(State != nullptr);

		// Carry over instance data count from parent.
		if (CompactState.Parent.IsValid())
		{
			const FCompactStateTreeState& ParentCompactState = StateTree->States[CompactState.Parent.Index];
			const int32 InstanceDataBegin = FirstInstanceDataIndex[CompactState.Parent.Index] + (int32)ParentCompactState.InstanceDataNum;
			FirstInstanceDataIndex[i] = InstanceDataBegin;

			CompactState.Depth = ParentCompactState.Depth + 1;
		}

		int32 InstanceDataIndex = FirstInstanceDataIndex[i];

		FStateTreeCompilerLogStateScope LogStateScope(State, Log);

		// Create parameters
		
		// Each state has their parameters as instance data.
		FInstancedStruct& Instance = InstanceStructs.AddDefaulted_GetRef();
		Instance.InitializeAs<FCompactStateTreeParameters>(State->Parameters.Parameters);
		FCompactStateTreeParameters& CompactStateTreeParameters = Instance.GetMutable<FCompactStateTreeParameters>(); 
			
		const int32 InstanceIndex = InstanceStructs.Num() - 1;
		if (const auto Validation = UE::StateTree::Compiler::IsValidIndex16(InstanceIndex); Validation.DidFail())
		{
			Validation.Log(Log, TEXT("InstanceIndex"));
			return false;
		}
		CompactState.ParameterTemplateIndex = FStateTreeIndex16(InstanceIndex);

		if (State->Type == EStateTreeStateType::Subtree)
		{
			CompactState.ParameterDataHandle = FStateTreeDataHandle(EStateTreeDataSourceType::SubtreeParameterData, InstanceDataIndex++, CompactStateHandle);
		}
		else
		{
			CompactState.ParameterDataHandle = FStateTreeDataHandle(EStateTreeDataSourceType::StateParameterData, InstanceDataIndex++, CompactStateHandle);
		}

		// @todo: We should be able to skip empty parameter data.

		const FString StatePath = State->GetPath(); 
		
		// Binding target
		FStateTreeBindableStructDesc LinkedParamsDesc = {
			StatePath,
			FName("Parameters"),
			State->Parameters.Parameters.GetPropertyBagStruct(),
			CompactState.ParameterDataHandle,
			EStateTreeBindableStructSource::StateParameter,
			State->Parameters.ID
		};

		if (!UE::StateTree::Compiler::ValidateNoLevelActorReferences(Log, LinkedParamsDesc, FStateTreeDataView(), FStateTreeDataView(CompactStateTreeParameters.Parameters.GetMutableValue())))
		{
			bSucceeded = false;
			continue;
		}

		// Add as binding source.
		BindingsCompiler.AddSourceStruct(LinkedParamsDesc);

		if (State->bHasRequiredEventToEnter)
		{
			CompactState.EventDataIndex = FStateTreeIndex16(InstanceDataIndex++);
			CompactState.RequiredEventToEnter.Tag = State->RequiredEventToEnter.Tag;
			CompactState.RequiredEventToEnter.PayloadStruct = State->RequiredEventToEnter.PayloadStruct;
			CompactState.bConsumeEventOnSelect = State->RequiredEventToEnter.bConsumeEventOnSelect;

			const FString StatePathWithConditions = StatePath + TEXT("/EnterConditions");

			FStateTreeBindableStructDesc Desc;
			Desc.StatePath = StatePathWithConditions,
			Desc.Struct = FStateTreeEvent::StaticStruct();
			Desc.Name = FName("Enter Event");
			Desc.ID = State->GetEventID();
			Desc.DataSource = EStateTreeBindableStructSource::StateEvent;
			Desc.DataHandle = FStateTreeDataHandle(EStateTreeDataSourceType::StateEvent, CompactState.EventDataIndex.Get(), CompactStateHandle);

			BindingsCompiler.AddSourceStruct(Desc);

			if (!CompactState.RequiredEventToEnter.IsValid())
			{
				Log.Reportf(EMessageSeverity::Error, Desc,
					TEXT("Event is marked as required, but isn't set up."));
				bSucceeded = false;
				continue;
			}
		}

		if (CompactState.Depth >= FStateTreeActiveStates::MaxStates)
		{
			Log.Reportf(EMessageSeverity::Error, LinkedParamsDesc,
				TEXT("Exceeds the maximum depth of execution (%u)"), FStateTreeActiveStates::MaxStates);
			bSucceeded = false;
			continue;
		}

		// Subtrees parameters cannot have bindings
		if (State->Type != EStateTreeStateType::Subtree)
		{
			FStateTreeIndex16 PropertyFunctionsBegin(Nodes.Num());
			if (!CreatePropertyFunctionsForStruct(LinkedParamsDesc.ID))
			{
				bSucceeded = false;
				continue;
			}

			FStateTreeIndex16 PropertyFunctionsEnd(Nodes.Num());
		
			if (PropertyFunctionsBegin == PropertyFunctionsEnd)
			{
				PropertyFunctionsBegin = FStateTreeIndex16::Invalid;
				PropertyFunctionsEnd = FStateTreeIndex16::Invalid;
			}

			if (!CreateBindingsForStruct(LinkedParamsDesc, FStateTreeDataView(CompactStateTreeParameters.Parameters.GetMutableValue()), PropertyFunctionsBegin, PropertyFunctionsEnd, CompactState.ParameterBindingsBatch))
			{
				bSucceeded = false;
				continue;
			}
		}

		// Create tasks
		const int32 TasksBegin = Nodes.Num();
		if (const auto Validation = UE::StateTree::Compiler::IsValidCount16(TasksBegin); Validation.DidFail())
		{
			Validation.Log(Log, TEXT("TasksBegin"));
			return false;
		}
		CompactState.TasksBegin = uint16(TasksBegin);
		
		TArrayView<FStateTreeEditorNode> Tasks;
		if (State->Tasks.Num())
		{
			Tasks = State->Tasks;
		}
		else if (State->SingleTask.Node.IsValid())
		{
			Tasks = TArrayView<FStateTreeEditorNode>(&State->SingleTask, 1);
		}
		
		bool bStateHasTransitionTasks = false;
		bool bCreateTaskSucceeded = true;
		for (FStateTreeEditorNode& TaskNode : Tasks)
		{
			// Silently ignore empty nodes.
			if (!TaskNode.Node.IsValid())
			{
				continue;
			}

			const FStateTreeDataHandle TaskDataHandle(EStateTreeDataSourceType::ActiveInstanceData, InstanceDataIndex++, CompactStateHandle);
			if (!CreateTask(State, TaskNode, TaskDataHandle))
			{
				bSucceeded = false;
				bCreateTaskSucceeded = false;
				continue;
			}

			const FStateTreeTaskBase& LastAddedTask = Nodes.Last().Get<FStateTreeTaskBase>();
			
			bStateHasTransitionTasks |= LastAddedTask.bShouldAffectTransitions;
		}

		CompactState.bHasTransitionTasks = bStateHasTransitionTasks;

		if (!bCreateTaskSucceeded)
		{
			continue;
		}
		
		const int32 TasksNum = Nodes.Num() - TasksBegin;
		if (const auto Validation = UE::StateTree::Compiler::IsValidCount8(TasksNum); Validation.DidFail())
		{
			Validation.Log(Log, TEXT("TasksNum"));
			return false;
		}

		const int32 InstanceDataNum = InstanceDataIndex - FirstInstanceDataIndex[i];
		if (const auto Validation = UE::StateTree::Compiler::IsValidCount8(InstanceDataNum); Validation.DidFail())
		{
			Validation.Log(Log, TEXT("InstanceDataNum"));
			return false;
		}

		CompactState.TasksNum = uint8(TasksNum);
		CompactState.InstanceDataNum = uint8(InstanceDataNum);

		if (!CreateBindingsForNodes(Tasks, FStateTreeIndex16(TasksBegin), InstanceStructs))
		{
			bSucceeded = false;
			continue;
		}
	}
	
	return bSucceeded;
}

bool FStateTreeCompiler::CreateStateTransitions()
{
	check(StateTree);

	bool bSucceeded = true;

	for (int32 i = 0; i < StateTree->States.Num(); i++)
	{
		FCompactStateTreeState& CompactState = StateTree->States[i];
		UStateTreeState* SourceState = SourceStates[i];
		check(SourceState != nullptr);

		FStateTreeCompilerLogStateScope LogStateScope(SourceState, Log);

		const FString StatePath = SourceState->GetPath();

		// Enter conditions.
		const int32 EnterConditionsBegin = Nodes.Num();
		if (const auto Validation = UE::StateTree::Compiler::IsValidCount16(EnterConditionsBegin); Validation.DidFail())
		{
			Validation.Log(Log, TEXT("EnterConditionsBegin"));
			return false;
		}
		CompactState.EnterConditionsBegin = uint16(EnterConditionsBegin);

		const FString StatePathWithConditions = StatePath + TEXT("/EnterConditions");
		if (!CreateConditions(*SourceState, StatePathWithConditions, SourceState->EnterConditions))
		{
			Log.Reportf(EMessageSeverity::Error,
				TEXT("Failed to create state enter condition."));
			bSucceeded = false;
			continue;
		}
		
		const int32 EnterConditionsNum = Nodes.Num() - EnterConditionsBegin;
		if (const auto Validation = UE::StateTree::Compiler::IsValidCount8(EnterConditionsNum); Validation.DidFail())
		{
			Validation.Log(Log, TEXT("EnterConditionsNum"));
			return false;
		}
		CompactState.EnterConditionsNum = uint8(EnterConditionsNum);

		if (!CreateBindingsForNodes(SourceState->EnterConditions, FStateTreeIndex16(EnterConditionsBegin), SharedInstanceStructs))
		{
			bSucceeded = false;
			continue;
		}

		// Check if any of the enter conditions require state completion events, and cache that.
		for (int32 ConditionIndex = (int32)CompactState.EnterConditionsBegin; ConditionIndex < Nodes.Num(); ConditionIndex++)
		{
			if (const FStateTreeConditionBase* Cond = Nodes[ConditionIndex].GetPtr<const FStateTreeConditionBase>())
			{
				if (Cond->bHasShouldCallStateChangeEvents)
				{
					CompactState.bHasStateChangeConditions = true;
					break;
				}
			}
		}
		
		// Linked state
		if (SourceState->Type == EStateTreeStateType::Linked)
		{
			// Make sure the linked state is not self or parent to this state.
			const UStateTreeState* LinkedParentState = nullptr;
			for (const UStateTreeState* State = SourceState; State != nullptr; State = State->Parent)
			{
				if (State->ID == SourceState->LinkedSubtree.ID)
				{
					LinkedParentState = State;
					break;
				}
			}
			
			if (LinkedParentState != nullptr)
			{
				Log.Reportf(EMessageSeverity::Error,
					TEXT("State is linked to it's parent subtree '%s', which will create infinite loop."),
					*LinkedParentState->Name.ToString());
				bSucceeded = false;
				continue;
			}

			// The linked state must be a subtree.
			const UStateTreeState* TargetState = GetState(SourceState->LinkedSubtree.ID);
			if (TargetState == nullptr)
			{
				Log.Reportf(EMessageSeverity::Error,
					TEXT("Failed to resolve linked subtree '%s'."),
					*SourceState->LinkedSubtree.Name.ToString());
				bSucceeded = false;
				continue;
			}
			
			if (TargetState->Type != EStateTreeStateType::Subtree)
			{
				Log.Reportf(EMessageSeverity::Error,
					TEXT("State '%s' is linked to subtree '%s', which is not a subtree."),
					*SourceState->Name.ToString(), *TargetState->Name.ToString());
				bSucceeded = false;
				continue;
			}
			
			CompactState.LinkedState = GetStateHandle(SourceState->LinkedSubtree.ID);
			
			if (!CompactState.LinkedState.IsValid())
			{
				Log.Reportf(EMessageSeverity::Error,
					TEXT("Failed to resolve linked subtree '%s'."),
					*SourceState->LinkedSubtree.Name.ToString());
				bSucceeded = false;
				continue;
			}
		}
		else if (SourceState->Type == EStateTreeStateType::LinkedAsset)
		{
			// Do not allow to link to the same asset (might create recursion)
			if (SourceState->LinkedAsset == StateTree)
			{
				Log.Reportf(EMessageSeverity::Error,
					TEXT("It is not allowed to link to the same tree, as it might create infinite loop."));
				bSucceeded = false;
				continue;
			}

			if (SourceState->LinkedAsset)
			{
				// Linked asset must have same schema.
				const UStateTreeSchema* LinkedAssetSchema = SourceState->LinkedAsset->GetSchema();

				if (!LinkedAssetSchema)
				{
					Log.Reportf(EMessageSeverity::Error,
						TEXT("Linked State Tree asset must have valid schema."));
					bSucceeded = false;
					continue;
				}
			
				check(Schema);
				if (LinkedAssetSchema->GetClass() != Schema->GetClass())
				{
					Log.Reportf(EMessageSeverity::Error,
						TEXT("Linked State Tree asset '%s' must have same schema class as this asset. Linked asset has '%s', expected '%s'."),
						*GetFullNameSafe(SourceState->LinkedAsset),
						*LinkedAssetSchema->GetClass()->GetDisplayNameText().ToString(),
						*Schema->GetClass()->GetDisplayNameText().ToString()
					);
					bSucceeded = false;
					continue;
				}
			}
			
			CompactState.LinkedAsset = SourceState->LinkedAsset;
		}

		// Transitions
		const int32 TransitionsBegin = StateTree->Transitions.Num();
		if (const auto Validation = UE::StateTree::Compiler::IsValidCount16(TransitionsBegin); Validation.DidFail())
		{
			Validation.Log(Log, TEXT("TransitionsBegin"));
			return false;
		}
		CompactState.TransitionsBegin = uint16(TransitionsBegin);

		bool bTransitionSucceeded = true;
		for (FStateTreeTransition& Transition : SourceState->Transitions)
		{
			const int32 TransitionIndex = StateTree->Transitions.Num();
			IDToTransition.Add(Transition.ID, TransitionIndex);

			FCompactStateTransition& CompactTransition = StateTree->Transitions.AddDefaulted_GetRef();
			CompactTransition.Trigger = Transition.Trigger;
			CompactTransition.Priority = Transition.Priority;

			CompactTransition.bTransitionEnabled = Transition.bTransitionEnabled;

			if (Transition.State.LinkType == EStateTreeTransitionType::NextSelectableState)
			{
				CompactTransition.Fallback = EStateTreeSelectionFallback::NextSelectableSibling;
			}

			if (Transition.bDelayTransition)
			{
				CompactTransition.Delay.Set(Transition.DelayDuration, Transition.DelayRandomVariance);
			}
			
			if (CompactState.SelectionBehavior == EStateTreeStateSelectionBehavior::TryFollowTransitions
				&& Transition.bDelayTransition)
			{
				Log.Reportf(EMessageSeverity::Warning,
					TEXT("Transition to '%s' with delay will be ignored during state selection."),
					*Transition.State.Name.ToString());
			}

			if (EnumHasAnyFlags(Transition.Trigger, EStateTreeTransitionTrigger::OnStateCompleted))
			{
				// Completion transitions dont have priority.
				CompactTransition.Priority = EStateTreeTransitionPriority::None;
				
				// Completion transitions cannot have delay.
				CompactTransition.Delay.Reset();

				// Completion transitions must have valid target state.
				if (Transition.State.LinkType == EStateTreeTransitionType::None)
				{
					Log.Reportf(EMessageSeverity::Error,
						TEXT("State completion transition to '%s' must have transition to valid state, 'None' not accepted."),
						*Transition.State.Name.ToString());
					bTransitionSucceeded = false;
					continue;
				}
			}
			
			CompactTransition.State = FStateTreeStateHandle::Invalid;
			if (!ResolveTransitionState(SourceState, Transition.State, CompactTransition.State))
			{
				bTransitionSucceeded = false;
				continue;
			}

			if (CompactTransition.State.IsValid()
				&& !CompactTransition.State.IsCompletionState())
			{
				FCompactStateTreeState& TransitionTargetState = StateTree->States[CompactTransition.State.Index];
				if (TransitionTargetState.Type == EStateTreeStateType::Subtree)
				{
					Log.Reportf(EMessageSeverity::Warning,
						TEXT("Transitioning directly to a Subtree State '%s' is not recommended, as it may have unexpected results. Subtree States should be used with Linked States instead."),
						*TransitionTargetState.Name.ToString());
				}
			}

			const FString StatePathWithTransition = StatePath + FString::Printf(TEXT("/Transition[%d]"), TransitionIndex - TransitionsBegin);
			
			if (Transition.Trigger == EStateTreeTransitionTrigger::OnEvent)
			{
				CompactTransition.RequiredEvent.Tag = Transition.RequiredEvent.Tag;
				CompactTransition.RequiredEvent.PayloadStruct = Transition.RequiredEvent.PayloadStruct;
				CompactTransition.bConsumeEventOnSelect = Transition.RequiredEvent.bConsumeEventOnSelect;

				FStateTreeBindableStructDesc Desc;
				Desc.StatePath = StatePathWithTransition;
				Desc.Struct = FStateTreeEvent::StaticStruct();
				Desc.Name = FName(TEXT("Transition Event"));
				Desc.ID = Transition.GetEventID();
				Desc.DataSource = EStateTreeBindableStructSource::TransitionEvent;
				Desc.DataHandle = FStateTreeDataHandle(EStateTreeDataSourceType::TransitionEvent, TransitionIndex);

				if (!Transition.RequiredEvent.IsValid())
				{
					Log.Reportf(EMessageSeverity::Error, Desc,
						TEXT("On Event Transition requires at least tag or payload to be set up."),
						*Transition.State.Name.ToString());
					bTransitionSucceeded = false;
					continue;
				}

				if (CompactTransition.State.IsValid()
					&& !CompactTransition.State.IsCompletionState())
				{
					FCompactStateTreeState& TransitionTargetState = StateTree->States[CompactTransition.State.Index];
					if (TransitionTargetState.RequiredEventToEnter.IsValid() && !TransitionTargetState.RequiredEventToEnter.IsSubsetOfAnotherDesc(CompactTransition.RequiredEvent))
					{
						Log.Reportf(EMessageSeverity::Error, Desc,
							TEXT("On Event transition to '%s' will never succeed as transition and state required events are incompatible."),
							*TransitionTargetState.Name.ToString());
						bTransitionSucceeded = false;
						continue;
					}
				}

				BindingsCompiler.AddSourceStruct(Desc);
			}

			const int32 ConditionsBegin = Nodes.Num();
			if (const auto Validation = UE::StateTree::Compiler::IsValidCount16(ConditionsBegin); Validation.DidFail())
			{
				Validation.Log(Log, TEXT("ConditionsBegin"));
				return false;
			}
			CompactTransition.ConditionsBegin = uint16(ConditionsBegin);
			
			if (!CreateConditions(*SourceState, StatePathWithTransition, Transition.Conditions))
			{
				Log.Reportf(EMessageSeverity::Error,
					TEXT("Failed to create condition for transition to '%s'."),
					*Transition.State.Name.ToString());
				bTransitionSucceeded = false;
				continue;
			}

			const int32 ConditionsNum = Nodes.Num() - ConditionsBegin;
			if (const auto Validation = UE::StateTree::Compiler::IsValidCount8(ConditionsNum); Validation.DidFail())
			{
				Validation.Log(Log, TEXT("ConditionsNum"));
				return false;
			}
			CompactTransition.ConditionsNum = uint8(ConditionsNum);

			if (!CreateBindingsForNodes(Transition.Conditions, FStateTreeIndex16(ConditionsBegin), SharedInstanceStructs))
			{
				bTransitionSucceeded = false;
				continue;
			}
		}

		if (!bTransitionSucceeded)
		{
			bSucceeded = false;
			continue;
		}
		
		const int32 TransitionsNum = StateTree->Transitions.Num() - TransitionsBegin;
		if (const auto Validation = UE::StateTree::Compiler::IsValidCount8(TransitionsNum); Validation.DidFail())
		{
			Validation.Log(Log, TEXT("TransitionsNum"));
			return false;
		}
		CompactState.TransitionsNum = uint8(TransitionsNum);
	}

	// @todo: Add test to check that all success/failure transition is possible (see editor).
	
	return bSucceeded;
}

bool FStateTreeCompiler::CreateStateConsiderations()
{
	check(StateTree);

	bool bSucceeded = true;

	for (int32 i = 0; i < StateTree->States.Num(); i++)
	{
		FCompactStateTreeState& CompactState = StateTree->States[i];
		UStateTreeState* SourceState = SourceStates[i];
		check(SourceState != nullptr);

		FStateTreeCompilerLogStateScope LogStateScope(SourceState, Log);

		const FString StatePath = SourceState->GetPath();

		const int32 UtilityConsiderationsBegin = Nodes.Num();
		if (const auto Validation = UE::StateTree::Compiler::IsValidCount16(UtilityConsiderationsBegin); Validation.DidFail())
		{
			Validation.Log(Log, TEXT("UtilityConsiderationsBegin"));
			bSucceeded = false;
			continue;
		}
		CompactState.UtilityConsiderationsBegin = uint16(UtilityConsiderationsBegin);

		const FString StatePathWithConsiderations = StatePath + TEXT("/Considerations");
		if (!CreateConsiderations(*SourceState, StatePathWithConsiderations, SourceState->Considerations))
		{
			Log.Reportf(EMessageSeverity::Error,
				TEXT("Failed to create state utility considerations."));
			bSucceeded = false;
			continue;
		}

		const int32 UtilityConsiderationsNum = Nodes.Num() - UtilityConsiderationsBegin;
		if (const auto Validation = UE::StateTree::Compiler::IsValidCount8(UtilityConsiderationsNum); Validation.DidFail())
		{
			Validation.Log(Log, TEXT("UtilityConsiderationsNum"));
			bSucceeded = false;
			continue;
		}
		CompactState.UtilityConsiderationsNum = uint8(UtilityConsiderationsNum);

		if (!CreateBindingsForNodes(SourceState->Considerations, FStateTreeIndex16(UtilityConsiderationsBegin), SharedInstanceStructs))
		{
			bSucceeded = false;
			continue;
		}
	}

	return bSucceeded;
}

bool FStateTreeCompiler::CreateBindingsForNodes(TConstArrayView<FStateTreeEditorNode> EditorNodes, FStateTreeIndex16 NodesBegin, TArrayView<FInstancedStruct> Instances)
{
	check(NodesBegin.IsValid());

	bool bSucceeded = true;

	int32 NodeIndex = NodesBegin.Get();
	for (const FStateTreeEditorNode& EditorNode : EditorNodes)
	{
		// Node might be an empty line in Editor.
		if (!EditorNode.Node.IsValid())
		{
			continue;
		}

		FStateTreeNodeBase& Node = Nodes[NodeIndex++].GetMutable<FStateTreeNodeBase>();

		const FStateTreeBindableStructDesc* BindableStruct = BindingsCompiler.GetSourceStructDescByID(EditorNode.ID);
		check(BindableStruct);

		FStateTreeIndex16 PropertyFunctionsBegin(Nodes.Num());
		if (!CreatePropertyFunctionsForStruct(EditorNode.ID))
		{
			bSucceeded = false;
			continue;
		}
		FStateTreeIndex16 PropertyFunctionsEnd(Nodes.Num());
		
		if (PropertyFunctionsBegin == PropertyFunctionsEnd)
		{
			PropertyFunctionsBegin = FStateTreeIndex16::Invalid;
			PropertyFunctionsEnd = FStateTreeIndex16::Invalid;
		}

		FStateTreeDataView InstanceView;
		check(Instances.IsValidIndex(Node.InstanceTemplateIndex.Get()));

		FInstancedStruct& Instance = Instances[Node.InstanceTemplateIndex.Get()];
		if (FStateTreeInstanceObjectWrapper* ObjectWrapper = Instance.GetMutablePtr<FStateTreeInstanceObjectWrapper>())
		{
			check(EditorNode.InstanceObject->GetClass() == ObjectWrapper->InstanceObject->GetClass());
			InstanceView = FStateTreeDataView(ObjectWrapper->InstanceObject);
		}
		else
		{
			check(EditorNode.Instance.GetScriptStruct() == Instance.GetScriptStruct());
			InstanceView = FStateTreeDataView(Instance);
		}

		if (!CreateBindingsForStruct(*BindableStruct, InstanceView, PropertyFunctionsBegin, PropertyFunctionsEnd, Node.BindingsBatch))
		{
			bSucceeded = false;
			continue;
		}
	}

	return bSucceeded;
}

bool FStateTreeCompiler::CreateBindingsForStruct(const FStateTreeBindableStructDesc& TargetStruct, FStateTreeDataView TargetValue, FStateTreeIndex16 PropertyFuncsBegin, FStateTreeIndex16 PropertyFuncsEnd, FStateTreeIndex16& OutBatchIndex)
{
	TArray<FStateTreePropertyPathBinding> CopyBindings;
	TArray<FStateTreePropertyPathBinding> ReferenceBindings;

	// Check that the bindings for this struct are still all valid.
	if (!GetAndValidateBindings(TargetStruct, TargetValue, CopyBindings, ReferenceBindings))
	{
		return false;
	}

	int32 BatchIndex = INDEX_NONE;

	// Compile batch copy for this struct, we pass in all the bindings, the compiler will pick up the ones for the target structs.
	if (!BindingsCompiler.CompileBatch(TargetStruct, CopyBindings, PropertyFuncsBegin, PropertyFuncsEnd, BatchIndex))
	{
		return false;
	}

	if (const auto Validation = UE::StateTree::Compiler::IsValidIndex16(BatchIndex); Validation.DidFail())
	{
		Validation.Log(Log, TEXT("BatchIndex"), TargetStruct);
		return false;
	}

	OutBatchIndex = FStateTreeIndex16(BatchIndex);

	if (!BindingsCompiler.CompileReferences(TargetStruct, ReferenceBindings, TargetValue, IDToStructValue))
	{
		return false;
	}

	return true;
}

bool FStateTreeCompiler::CreatePropertyFunctionsForStruct(FGuid StructID)
{
	for (const FStateTreePropertyPathBinding& Binding : EditorData->EditorBindings.GetBindings())
	{
		if (Binding.GetTargetPath().GetStructID() != StructID)
		{
			continue;
		}

		const FConstStructView NodeView = Binding.GetPropertyFunctionNode();
		if (!NodeView.IsValid())
		{
			continue;
		}

		const FStateTreeEditorNode& FuncEditorNode = NodeView.Get<const FStateTreeEditorNode>();
		if (!CreatePropertyFunction(FuncEditorNode))
		{
			return false;
		}
	}

	return true;
}

bool FStateTreeCompiler::CreatePropertyFunction(const FStateTreeEditorNode& FuncEditorNode)
{
	if (!CreatePropertyFunctionsForStruct(FuncEditorNode.ID))
	{
		return false;
	}

	FStateTreeNodeBase* Node = CreateNodeWithSharedInstanceData(nullptr, UE::StateTree::Editor::PropertyFunctionStateName, FuncEditorNode, EStateTreeBindableStructSource::PropertyFunction);
	if(Node == nullptr)
	{
		return false;
	}

	const FStateTreeBindableStructDesc* BindableStruct = BindingsCompiler.GetSourceStructDescByID(FuncEditorNode.ID);
	check(BindableStruct);

	FStateTreeDataView InstanceView;
	check(SharedInstanceStructs.IsValidIndex(Node->InstanceTemplateIndex.Get()));

	FInstancedStruct& Instance = SharedInstanceStructs[Node->InstanceTemplateIndex.Get()];
	if (FStateTreeInstanceObjectWrapper* ObjectWrapper = Instance.GetMutablePtr<FStateTreeInstanceObjectWrapper>())
	{
		check(FuncEditorNode.InstanceObject->GetClass() == ObjectWrapper->InstanceObject->GetClass());
		InstanceView = FStateTreeDataView(ObjectWrapper->InstanceObject);
	}
	else
	{
		check(FuncEditorNode.Instance.GetScriptStruct() == Instance.GetScriptStruct());
		InstanceView = FStateTreeDataView(Instance);
	}

	return CreateBindingsForStruct(*BindableStruct, InstanceView, FStateTreeIndex16::Invalid, FStateTreeIndex16::Invalid, Node->BindingsBatch);
}

template<class T>
T* FStateTreeCompiler::CreateNodeWithSharedInstanceData(UStateTreeState* State, const FString& StatePath, const FStateTreeEditorNode& EditorNode, EStateTreeBindableStructSource StructSource)
{
	if (!EditorNode.Node.IsValid())
	{
		return nullptr;
	}

	check(EditorNode.Node.GetScriptStruct()->IsChildOf<T>());

	FStateTreeBindableStructDesc StructDesc;
	StructDesc.StatePath = StatePath;
	StructDesc.ID = EditorNode.ID;
	StructDesc.Name = EditorNode.GetName();
	StructDesc.DataSource = StructSource;

	// Check that item has valid instance initialized.
	if (!EditorNode.Instance.IsValid() && EditorNode.InstanceObject == nullptr)
	{
		Log.Reportf(EMessageSeverity::Error, StructDesc,
			TEXT("Malformed node, missing instance value."));
		return nullptr;
	}

	// Copy the node
	IDToNode.Add(EditorNode.ID, Nodes.Num());
	FInstancedStruct& RawNode = Nodes.Add_GetRef(EditorNode.Node);
	InstantiateStructSubobjects(RawNode);

	FStateTreeNodeBase& Node = RawNode.GetMutable<FStateTreeNodeBase>();

	// Update node name as description for runtime.
	Node.Name = EditorNode.GetName();

	FStateTreeDataView InstanceDataView;
	
	if (EditorNode.Instance.IsValid())
	{
		// Struct instance
		const int32 InstanceIndex = SharedInstanceStructs.Add(EditorNode.Instance);
		InstantiateStructSubobjects(SharedInstanceStructs[InstanceIndex]);

		// Create binding source struct descriptor.
		StructDesc.Struct = EditorNode.Instance.GetScriptStruct();

		if (const auto Validation = UE::StateTree::Compiler::IsValidIndex16(InstanceIndex); Validation.DidFail())
		{
			Validation.Log(Log, TEXT("InstanceIndex"), StructDesc);
			return nullptr;
		}
		Node.InstanceTemplateIndex = FStateTreeIndex16(InstanceIndex);
		Node.InstanceDataHandle = FStateTreeDataHandle(EStateTreeDataSourceType::SharedInstanceData, InstanceIndex);
		InstanceDataView = FStateTreeDataView(SharedInstanceStructs[InstanceIndex]);
	}
	else
	{
		// Object Instance
		check(EditorNode.InstanceObject != nullptr);

		UObject* Instance = DuplicateObject(EditorNode.InstanceObject, StateTree);
		FInstancedStruct Wrapper;
		Wrapper.InitializeAs<FStateTreeInstanceObjectWrapper>(Instance);
		const int32 InstanceIndex = SharedInstanceStructs.Add(MoveTemp(Wrapper));
		
		// Create binding source struct descriptor.
		StructDesc.Struct = Instance->GetClass();

		if (const auto Validation = UE::StateTree::Compiler::IsValidIndex16(InstanceIndex); Validation.DidFail())
		{
			Validation.Log(Log, TEXT("InstanceIndex"), StructDesc);
			return nullptr;
		}
		Node.InstanceTemplateIndex = FStateTreeIndex16(InstanceIndex);
		Node.InstanceDataHandle = FStateTreeDataHandle(EStateTreeDataSourceType::SharedInstanceDataObject, InstanceIndex);
		InstanceDataView = FStateTreeDataView(Instance);
	}

	StructDesc.DataHandle = Node.InstanceDataHandle;
	
	if (!CompileAndValidateNode(State, StructDesc, RawNode, InstanceDataView))
	{
		return nullptr;
	}

	// Mark the struct as binding source.
	BindingsCompiler.AddSourceStruct(StructDesc);
	
	check(RawNode.GetScriptStruct()->IsChildOf<T>());
	return RawNode.GetMutablePtr<T>();
}

bool FStateTreeCompiler::ResolveTransitionState(const UStateTreeState* SourceState, const FStateTreeStateLink& Link, FStateTreeStateHandle& OutTransitionHandle) const 
{
	if (Link.LinkType == EStateTreeTransitionType::GotoState)
	{
		// Warn if goto state points to another subtree.
		if (const UStateTreeState* TargetState = GetState(Link.ID))
		{
			if (SourceState && TargetState->GetRootState() != SourceState->GetRootState())
			{
				Log.Reportf(EMessageSeverity::Warning,
					TEXT("Target state '%s' is in different subtree. Verify that this is intentional."),
					*Link.Name.ToString());
			}

			if (TargetState->SelectionBehavior == EStateTreeStateSelectionBehavior::None)
			{
				Log.Reportf(EMessageSeverity::Error,
					TEXT("The target State '%s' is not selectable, it's selection behavior is set to None."),
					*Link.Name.ToString());
				return false;
			}
		}
		
		OutTransitionHandle = GetStateHandle(Link.ID);
		if (!OutTransitionHandle.IsValid())
		{
			Log.Reportf(EMessageSeverity::Error,
				TEXT("Failed to resolve transition to state '%s'."),
				*Link.Name.ToString());
			return false;
		}
	}
	else if (Link.LinkType == EStateTreeTransitionType::NextState || Link.LinkType == EStateTreeTransitionType::NextSelectableState)
	{
		// Find next state.
		const UStateTreeState* NextState = SourceState ? SourceState->GetNextSelectableSiblingState() : nullptr;
		if (NextState == nullptr)
		{
			Log.Reportf(EMessageSeverity::Error,
				TEXT("Failed to resolve transition, there's no selectable next state."));
			return false;
		}
		OutTransitionHandle = GetStateHandle(NextState->ID);
		if (!OutTransitionHandle.IsValid())
		{
			Log.Reportf(EMessageSeverity::Error,
				TEXT("Failed to resolve transition next state, no handle found for '%s'."),
				*NextState->Name.ToString());
			return false;
		}
	}
	else if(Link.LinkType == EStateTreeTransitionType::Failed)
	{
		OutTransitionHandle = FStateTreeStateHandle::Failed;
		return true;
	}
	else if(Link.LinkType == EStateTreeTransitionType::Succeeded)
	{
		OutTransitionHandle = FStateTreeStateHandle::Succeeded;
		return true;
	}
	else if(Link.LinkType == EStateTreeTransitionType::None)
	{
		OutTransitionHandle = FStateTreeStateHandle::Invalid;
		return true;
	}
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	else if (Link.LinkType == EStateTreeTransitionType::NotSet)
	{
		OutTransitionHandle = FStateTreeStateHandle::Invalid;
		return false;
	}
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	
	return true;
}

bool FStateTreeCompiler::CreateCondition(UStateTreeState& State, const FString& StatePath, const FStateTreeEditorNode& CondNode, const EStateTreeExpressionOperand Operand, const int8 DeltaIndent)
{
	if (!CondNode.Node.IsValid())
	{
		// Empty line in conditions array, just silently ignore.
		return true;
	}

	if (FStateTreeConditionBase* Cond = CreateNodeWithSharedInstanceData<FStateTreeConditionBase>(&State, StatePath, CondNode, EStateTreeBindableStructSource::Condition))
	{
		Cond->Operand = Operand;
		Cond->DeltaIndent = DeltaIndent;
		return true;
	}

	return false;
}

bool FStateTreeCompiler::CreateConsiderations(UStateTreeState& State, const FString& StatePath, TConstArrayView<FStateTreeEditorNode> Considerations)
{
	if (State.Considerations.Num() != 0)
	{
		if (!State.Parent
			|| (State.Parent->SelectionBehavior != EStateTreeStateSelectionBehavior::TrySelectChildrenWithHighestUtility
				&& State.Parent->SelectionBehavior != EStateTreeStateSelectionBehavior::TrySelectChildrenAtRandomWeightedByUtility))
		{
			Log.Reportf(EMessageSeverity::Warning, TEXT("State's Utility Considerations data are compiled but they don't have effect."
					"The Utility Considerations are used only when parent State's Selection Behavior is:"
					"\"Try Select Children with Highest Utility\" or \"Try Select Children At Random Weighted By Utility\"."));
		}
	}

	for (int32 Index = 0; Index < Considerations.Num(); Index++)
	{
		const bool bIsFirst = Index == 0;
		const FStateTreeEditorNode& ConsiderationNode = Considerations[Index];
		// First operand should be copy as we dont have a previous item to operate on.
		const EStateTreeExpressionOperand Operand = bIsFirst ? EStateTreeExpressionOperand::Copy : ConsiderationNode.ExpressionOperand;
		// First indent must be 0 to make the parentheses calculation match.
		const int32 CurrIndent = bIsFirst ? 0 : FMath::Clamp((int32)ConsiderationNode.ExpressionIndent, 0, UE::StateTree::MaxExpressionIndent);
		// Next indent, or terminate at zero.
		const int32 NextIndent = Considerations.IsValidIndex(Index + 1) ? FMath::Clamp((int32)Considerations[Index + 1].ExpressionIndent, 0, UE::StateTree::MaxExpressionIndent) : 0;

		const int32 DeltaIndent = NextIndent - CurrIndent;

		if (!CreateConsideration(State, StatePath, ConsiderationNode, Operand, (int8)DeltaIndent))
		{
			return false;
		}
	}

	return true;
}

bool FStateTreeCompiler::CreateConsideration(UStateTreeState& State, const FString& StatePath, const FStateTreeEditorNode& ConsiderationNode, const EStateTreeExpressionOperand Operand, const int8 DeltaIndent)
{
	if (!ConsiderationNode.Node.IsValid())
	{
		// Empty line in conditions array, just silently ignore.
		return true;
	}

	if (FStateTreeConsiderationBase* Consideration = CreateNodeWithSharedInstanceData<FStateTreeConsiderationBase>(&State, StatePath, ConsiderationNode, EStateTreeBindableStructSource::Consideration))
	{
		Consideration->Operand = Operand;
		Consideration->DeltaIndent = DeltaIndent;
		return true;
	}

	return false;
}

bool FStateTreeCompiler::CompileAndValidateNode(const UStateTreeState* SourceState, const FStateTreeBindableStructDesc& NodeDesc, FStructView NodeView, const FStateTreeDataView InstanceData)
{
	if (!NodeView.IsValid())
	{
		return false;
	}
	
	FStateTreeNodeBase& Node = NodeView.Get<FStateTreeNodeBase>();
	check(InstanceData.IsValid());

	auto ValidateStateLinks = [this, SourceState](TPropertyValueIterator<FStructProperty> It)
	{
		for ( ; It; ++It)
		{
			if (It->Key->Struct == TBaseStructure<FStateTreeStateLink>::Get())
			{
				FStateTreeStateLink& StateLink = *static_cast<FStateTreeStateLink*>(const_cast<void*>(It->Value));

				if (!ResolveTransitionState(SourceState, StateLink, StateLink.StateHandle))
				{
					return false;
				}
			}
		}

		return true;
	};
	
	// Validate any state links.
	if (!ValidateStateLinks(TPropertyValueIterator<FStructProperty>(InstanceData.GetStruct(), InstanceData.GetMutableMemory())))
	{
		return false;
	}
	if (!ValidateStateLinks(TPropertyValueIterator<FStructProperty>(NodeView.GetScriptStruct(), NodeView.GetMemory())))
	{
		return false;
	}

	TArray<FText> ValidationErrors;
	const EDataValidationResult Result = Node.Compile(InstanceData, ValidationErrors);

	if (Result == EDataValidationResult::Invalid && ValidationErrors.IsEmpty())
	{
		Log.Report(EMessageSeverity::Error, NodeDesc, TEXT("Node validation failed."));
	}
	else
	{
		const EMessageSeverity::Type Severity = Result == EDataValidationResult::Invalid ? EMessageSeverity::Error : EMessageSeverity::Warning;
		for (const FText& Error : ValidationErrors)
		{
			Log.Report(Severity, NodeDesc, Error.ToString());
		}
	}

	// Make sure there's no level actor references in the data.
	if (!UE::StateTree::Compiler::ValidateNoLevelActorReferences(Log, NodeDesc, NodeView, InstanceData))
	{
		return false;
	}
	
	return Result != EDataValidationResult::Invalid;
}

bool FStateTreeCompiler::CreateTask(UStateTreeState* State, const FStateTreeEditorNode& TaskNode, const FStateTreeDataHandle TaskDataHandle)
{
	if (!TaskNode.Node.IsValid())
	{
		return false;
	}
	
	// Create binding source struct descriptor.
	FStateTreeBindableStructDesc StructDesc;
	StructDesc.StatePath = State ? State->GetPath() : UE::StateTree::Editor::GlobalStateName;
	StructDesc.ID = TaskNode.ID;
	StructDesc.Name = TaskNode.GetName();
	StructDesc.DataSource = EStateTreeBindableStructSource::Task;

	// Check that node has valid instance initialized.
	if (!TaskNode.Instance.IsValid() && TaskNode.InstanceObject == nullptr)
	{
		Log.Reportf(EMessageSeverity::Error, StructDesc,
			TEXT("Malformed task, missing instance value."));
		return false;
	}

	// Copy the task
	IDToNode.Add(TaskNode.ID, Nodes.Num());
	FInstancedStruct& Node = Nodes.Add_GetRef(TaskNode.Node);
	InstantiateStructSubobjects(Node);
	
	FStateTreeTaskBase& Task = Node.GetMutable<FStateTreeTaskBase>();
	FStateTreeDataView InstanceDataView;

	// Update task name as description for runtime.
	Task.Name = TaskNode.GetName();
	
	if (TaskNode.Instance.IsValid())
	{
		// Struct Instance
		const int32 InstanceIndex = InstanceStructs.Add(TaskNode.Instance);
		InstantiateStructSubobjects(InstanceStructs[InstanceIndex]);

		// Create binding source struct descriptor.
		StructDesc.Struct = TaskNode.Instance.GetScriptStruct();

		if (const auto Validation = UE::StateTree::Compiler::IsValidIndex16(InstanceIndex); Validation.DidFail())
		{
			Validation.Log(Log, TEXT("InstanceIndex"), StructDesc);
			return false;
		}
		Task.InstanceTemplateIndex = FStateTreeIndex16(InstanceIndex);
		Task.InstanceDataHandle = TaskDataHandle;
		InstanceDataView = FStateTreeDataView(InstanceStructs[InstanceIndex]);
	}
	else
	{
		// Object Instance
		check(TaskNode.InstanceObject != nullptr);

		UObject* Instance = DuplicateObject(TaskNode.InstanceObject, StateTree);
		FInstancedStruct Wrapper;
		Wrapper.InitializeAs<FStateTreeInstanceObjectWrapper>(Instance);
		const int32 InstanceIndex = InstanceStructs.Add(MoveTemp(Wrapper));

		// Create binding source struct descriptor.
		StructDesc.Struct = Instance->GetClass();

		if (const auto Validation = UE::StateTree::Compiler::IsValidIndex16(InstanceIndex); Validation.DidFail())
		{
			Validation.Log(Log, TEXT("InstanceIndex"), StructDesc);
			return false;
		}
		Task.InstanceTemplateIndex = FStateTreeIndex16(InstanceIndex);
		Task.InstanceDataHandle = TaskDataHandle.ToObjectSource();
		InstanceDataView = FStateTreeDataView(Instance);
	}

	StructDesc.DataHandle = Task.InstanceDataHandle;

	if (!CompileAndValidateNode(State, StructDesc, Node,  InstanceDataView))
	{
		return false;
	}

	// Mark the instance as binding source.
	BindingsCompiler.AddSourceStruct(StructDesc);
	
	return true;
}

bool FStateTreeCompiler::CreateEvaluator(const FStateTreeEditorNode& EvalNode, const FStateTreeDataHandle EvalDataHandle)
{
	// Silently ignore empty nodes.
	if (!EvalNode.Node.IsValid())
	{
		return true;
	}

	// Create binding source struct descriptor.
	FStateTreeBindableStructDesc StructDesc;
	StructDesc.StatePath = UE::StateTree::Editor::GlobalStateName;
    StructDesc.ID = EvalNode.ID;
	StructDesc.Name = EvalNode.GetName();
	StructDesc.DataSource = EStateTreeBindableStructSource::Evaluator;

    // Check that node has valid instance initialized.
    if (!EvalNode.Instance.IsValid() && EvalNode.InstanceObject == nullptr)
    {
        Log.Reportf(EMessageSeverity::Error, StructDesc,
        	TEXT("Malformed evaluator, missing instance value."));
        return false;
    }

	// Copy the evaluator
	IDToNode.Add(EvalNode.ID, Nodes.Num());
	FInstancedStruct& Node = Nodes.Add_GetRef(EvalNode.Node);
	InstantiateStructSubobjects(Node);
	
	FStateTreeEvaluatorBase& Eval = Node.GetMutable<FStateTreeEvaluatorBase>();
	FStateTreeDataView InstanceDataView;

	// Update eval name as description for runtime.
	Eval.Name = EvalNode.GetName();

	if (EvalNode.Instance.IsValid())
	{
		// Struct Instance
		const int32 InstanceIndex = InstanceStructs.Add(EvalNode.Instance);
		InstantiateStructSubobjects(InstanceStructs[InstanceIndex]);

		// Create binding source struct descriptor.
		StructDesc.Struct = EvalNode.Instance.GetScriptStruct();

		if (const auto Validation = UE::StateTree::Compiler::IsValidIndex16(InstanceIndex); Validation.DidFail())
		{
			Validation.Log(Log, TEXT("InstanceIndex"), StructDesc);
			return false;
		}
		Eval.InstanceTemplateIndex = FStateTreeIndex16(InstanceIndex);
		Eval.InstanceDataHandle = EvalDataHandle;
		InstanceDataView = FStateTreeDataView(InstanceStructs[InstanceIndex]);
	}
	else
	{
		// Object Instance
		check(EvalNode.InstanceObject != nullptr);

		UObject* Instance = DuplicateObject(EvalNode.InstanceObject, StateTree);
		FInstancedStruct Wrapper;
		Wrapper.InitializeAs<FStateTreeInstanceObjectWrapper>(Instance);
		const int32 InstanceIndex = InstanceStructs.Add(MoveTemp(Wrapper));
		
		// Create binding source struct descriptor.
		StructDesc.Struct = Instance->GetClass();

		if (const auto Validation = UE::StateTree::Compiler::IsValidIndex16(InstanceIndex); Validation.DidFail())
		{
			Validation.Log(Log, TEXT("InstanceIndex"), StructDesc);
			return false;
		}
		Eval.InstanceTemplateIndex = FStateTreeIndex16(InstanceIndex);
		Eval.InstanceDataHandle = EvalDataHandle.ToObjectSource();
		InstanceDataView = FStateTreeDataView(Instance);
	}

	StructDesc.DataHandle = Eval.InstanceDataHandle;

	if (!CompileAndValidateNode(nullptr, StructDesc, Node,  InstanceDataView))
	{
		return false;
	}

	// Mark the instance as binding source.
	BindingsCompiler.AddSourceStruct(StructDesc);

	return true;
}

bool FStateTreeCompiler::IsPropertyOfTypeOrChild(UScriptStruct& Type, const FStateTreeBindableStructDesc& Struct, FStateTreePropertyPath Path) const
{
	TArray<FStateTreePropertyPathIndirection> Indirection;
	const bool bResolved = Path.ResolveIndirections(Struct.Struct, Indirection);
	
	if (bResolved && Indirection.Num() > 0)
	{
		check(Indirection.Last().GetProperty());
		if (const FProperty* OwnerProperty = Indirection.Last().GetProperty()->GetOwnerProperty())
		{
			if (const FStructProperty* OwnerStructProperty = CastField<FStructProperty>(OwnerProperty))
			{
				return OwnerStructProperty->Struct->IsChildOf(&Type);
			}
		}
	}
	return false;
}

bool FStateTreeCompiler::ValidateStructRef(const FStateTreeBindableStructDesc& SourceStruct, FStateTreePropertyPath SourcePath,
											const FStateTreeBindableStructDesc& TargetStruct, FStateTreePropertyPath TargetPath) const
{
	FString ResolveError;
	TArray<FStateTreePropertyPathIndirection> TargetIndirection;
	if (!TargetPath.ResolveIndirections(TargetStruct.Struct, TargetIndirection, &ResolveError))
	{
		// This will later be reported by the bindings compiler.
		Log.Reportf(EMessageSeverity::Error, TargetStruct, TEXT("Failed to resolve binding path in %s: %s"), *TargetStruct.ToString(), *ResolveError);
		return false;
	}
	const FProperty* TargetLeafProperty = TargetIndirection.Num() > 0 ? TargetIndirection.Last().GetProperty() : nullptr;

	// Early out if the target is not FStateTreeStructRef.
	const FStructProperty* TargetStructProperty = CastField<FStructProperty>(TargetLeafProperty);
	if (TargetStructProperty == nullptr || TargetStructProperty->Struct != TBaseStructure<FStateTreeStructRef>::Get())
	{
		return true;
	}

	FString TargetBaseStructName;
	const UScriptStruct* TargetBaseStruct = UE::StateTree::Compiler::GetBaseStructFromMetaData(TargetStructProperty, TargetBaseStructName);
	if (TargetBaseStruct == nullptr)
	{
		Log.Reportf(EMessageSeverity::Error, TargetStruct,
				TEXT("Could not find base struct type '%s' for target %s'."),
				*TargetBaseStructName, *UE::StateTree::GetDescAndPathAsString(TargetStruct, TargetPath));
		return false;
	}

	TArray<FStateTreePropertyPathIndirection> SourceIndirection;
	if (!SourcePath.ResolveIndirections(SourceStruct.Struct, SourceIndirection, &ResolveError))
	{
		// This will later be reported by the bindings compiler.
		Log.Reportf(EMessageSeverity::Error, SourceStruct, TEXT("Failed to resolve binding path in %s: %s"), *SourceStruct.ToString(), *ResolveError);
		return false;
	}
	const FProperty* SourceLeafProperty = SourceIndirection.Num() > 0 ? SourceIndirection.Last().GetProperty() : nullptr;

	// Exit if the source is not a struct property.
	const FStructProperty* SourceStructProperty = CastField<FStructProperty>(SourceLeafProperty);
	if (SourceStructProperty == nullptr)
	{
		return true;
	}
	
	if (SourceStructProperty->Struct == TBaseStructure<FStateTreeStructRef>::Get())
	{
		// Source is struct ref too, check the types match.
		FString SourceBaseStructName;
		const UScriptStruct* SourceBaseStruct = UE::StateTree::Compiler::GetBaseStructFromMetaData(SourceStructProperty, SourceBaseStructName);
		if (SourceBaseStruct == nullptr)
		{
			Log.Reportf(EMessageSeverity::Error, TargetStruct,
					TEXT("Could not find base struct '%s' for binding source %s."),
					*SourceBaseStructName, *UE::StateTree::GetDescAndPathAsString(SourceStruct, SourcePath));
			return false;
		}

		if (SourceBaseStruct->IsChildOf(TargetBaseStruct) == false)
		{
			Log.Reportf(EMessageSeverity::Error, TargetStruct,
						TEXT("Type mismatch between source %s and target %s types, '%s' is not child of '%s'."),
						*UE::StateTree::GetDescAndPathAsString(SourceStruct, SourcePath),
						*UE::StateTree::GetDescAndPathAsString(TargetStruct, TargetPath),
						*GetNameSafe(SourceBaseStruct), *GetNameSafe(TargetBaseStruct));
			return false;
		}
	}
	else
	{
		if (!SourceStructProperty->Struct || SourceStructProperty->Struct->IsChildOf(TargetBaseStruct) == false)
		{
			Log.Reportf(EMessageSeverity::Error, TargetStruct,
						TEXT("Type mismatch between source %s and target %s types, '%s' is not child of '%s'."),
						*UE::StateTree::GetDescAndPathAsString(SourceStruct, SourcePath),
						*UE::StateTree::GetDescAndPathAsString(TargetStruct, TargetPath),
						*GetNameSafe(SourceStructProperty->Struct), *GetNameSafe(TargetBaseStruct));
			return false;
		}
	}

	return true;
}


bool FStateTreeCompiler::GetAndValidateBindings(const FStateTreeBindableStructDesc& TargetStruct, FStateTreeDataView TargetValue, TArray<FStateTreePropertyPathBinding>& OutCopyBindings, TArray<FStateTreePropertyPathBinding>& OutReferenceBindings) const
{
	check(EditorData);
	
	OutCopyBindings.Reset();
	OutReferenceBindings.Reset();

	// If target struct is not set, nothing to do.
	if (TargetStruct.Struct == nullptr)
	{
		return true;
	}

	bool bSucceeded = true;

	for (FStateTreePropertyPathBinding& Binding : EditorData->EditorBindings.GetMutableBindings())
	{
		if (Binding.GetTargetPath().GetStructID() != TargetStruct.ID)
		{
			continue;
		}

		// Source must be one of the source structs we have discovered in the tree.
		const FGuid SourceStructID = Binding.GetSourcePath().GetStructID();
		const FStateTreeBindableStructDesc* SourceStruct = BindingsCompiler.GetSourceStructDescByID(SourceStructID);
		if (!SourceStruct)
		{
			Log.Reportf(EMessageSeverity::Error, TargetStruct,
						TEXT("Failed to find binding source property '%s' for target %s."),
						*Binding.GetSourcePath().ToString(), *UE::StateTree::GetDescAndPathAsString(TargetStruct, Binding.GetTargetPath()));
			bSucceeded = false;
			continue;
		}

		// Update path instance types from latest data. E.g. binding may have been created for instanced object of type FooB, and changed to FooA.
 		// @todo: not liking how this mutates the Binding.TargetPath, but currently we dont track well the instanced object changes.

		if (!Binding.GetMutableTargetPath().UpdateSegmentsFromValue(TargetValue))
		{
			Log.Reportf(EMessageSeverity::Error, TargetStruct,
						TEXT("Malformed target property path for binding source property '%s' for target %s."),
						*Binding.GetSourcePath().ToString(), *UE::StateTree::GetDescAndPathAsString(TargetStruct, Binding.GetTargetPath()));
			bSucceeded = false;
			continue;
		}
		
		// Source must be accessible by the target struct via all execution paths.
		TArray<FStateTreeBindableStructDesc> AccessibleStructs;
		EditorData->GetAccessibleStructs(Binding.GetTargetPath().GetStructID(), AccessibleStructs);

		const bool bSourceAccessible = AccessibleStructs.ContainsByPredicate([SourceStructID](const FStateTreeBindableStructDesc& Structs)
			{
				return (Structs.ID == SourceStructID);
			});

		if (!bSourceAccessible)
		{
			Log.Reportf(EMessageSeverity::Error, TargetStruct,
						TEXT("Property at %s cannot be bound to %s, because the binding source %s is not updated before %s in the tree."),
						*UE::StateTree::GetDescAndPathAsString(*SourceStruct, Binding.GetSourcePath()),
						*UE::StateTree::GetDescAndPathAsString(TargetStruct, Binding.GetTargetPath()),
						*SourceStruct->ToString(), *TargetStruct.ToString());
			bSucceeded = false;
			continue;
		}

		if (!IDToStructValue.Contains(SourceStructID))
		{
			Log.Reportf(EMessageSeverity::Error, TargetStruct,
				TEXT("Failed to find value for binding source property '%s' for target %s."),
				*Binding.GetSourcePath().ToString(), *UE::StateTree::GetDescAndPathAsString(TargetStruct, Binding.GetTargetPath()));
			bSucceeded = false;
			continue;
		}

		// Update the source structs only if we have value for it. For some sources (e.g. context structs) we know only type, and in that case there are no instance structs.
		const FStateTreeDataView SourceValue = IDToStructValue[SourceStructID];
		if (SourceValue.IsValid())
		{
			if (!Binding.GetMutableSourcePath().UpdateSegmentsFromValue(SourceValue))
			{
				Log.Reportf(EMessageSeverity::Error, TargetStruct,
					TEXT("Malformed target property path for binding source property '%s' for source %s."),
					*Binding.GetSourcePath().ToString(), *UE::StateTree::GetDescAndPathAsString(TargetStruct, Binding.GetTargetPath()));
				bSucceeded = false;
				continue;
			}
		}

		if (!SourceStruct->DataHandle.IsValid())
		{
			Log.Reportf(EMessageSeverity::Error, TargetStruct,
				TEXT("Malformed source'%s for property binding property '%s'."),
				*UE::StateTree::GetDescAndPathAsString(*SourceStruct, Binding.GetSourcePath()), *Binding.GetSourcePath().ToString());
			bSucceeded = false;
			continue;
		}
		
		FStateTreePropertyPathBinding BindingCopy(Binding);
		BindingCopy.SetSourceDataHandle(SourceStruct->DataHandle);

		// Special case for AnyEnum. StateTreeBindingExtension allows AnyEnums to bind to other enum types.
		// The actual copy will be done via potential type promotion copy, into the value property inside the AnyEnum.
		// We amend the paths here to point to the 'Value' property.
		const bool bSourceIsAnyEnum = IsPropertyOfTypeOrChild(*TBaseStructure<FStateTreeAnyEnum>::Get(), *SourceStruct, Binding.GetSourcePath());
		const bool bTargetIsAnyEnum = IsPropertyOfTypeOrChild(*TBaseStructure<FStateTreeAnyEnum>::Get(), TargetStruct, Binding.GetTargetPath());
		if (bSourceIsAnyEnum || bTargetIsAnyEnum)
		{
			if (bSourceIsAnyEnum)
			{
				BindingCopy.GetMutableSourcePath().AddPathSegment(GET_MEMBER_NAME_STRING_CHECKED(FStateTreeAnyEnum, Value));
			}
			if (bTargetIsAnyEnum)
			{
				BindingCopy.GetMutableTargetPath().AddPathSegment(GET_MEMBER_NAME_STRING_CHECKED(FStateTreeAnyEnum, Value));
			}
		}

		if (IsPropertyOfTypeOrChild(*FStateTreePropertyRef::StaticStruct(), TargetStruct, Binding.GetTargetPath()))
		{
			OutReferenceBindings.Add(BindingCopy);
		}
		else
		{
			OutCopyBindings.Add(BindingCopy);
		}
		
		// Check if the bindings is for struct ref and validate the types.
		if (!ValidateStructRef(*SourceStruct, Binding.GetSourcePath(), TargetStruct, Binding.GetTargetPath()))
		{
			bSucceeded = false;
			continue;
		}
	}

	if (!bSucceeded)
	{
		return false;
	}

	auto IsPropertyBound = [](const FName& PropertyName, TConstArrayView<FStateTreePropertyPathBinding> Bindings)
	{
		return Bindings.ContainsByPredicate([&PropertyName](const FStateTreePropertyPathBinding& Binding)
			{
				// We're looping over just the first level of properties on the struct, so we assume that the path is just one item
				// (or two in case of AnyEnum, because we expand the path to Property.Value, see code above).
				return Binding.GetTargetPath().GetSegments().Num() >= 1 && Binding.GetTargetPath().GetSegments()[0].GetName() == PropertyName;
			});
	};
	
	// Validate that Input and Context bindings
	for (TFieldIterator<FProperty> It(TargetStruct.Struct); It; ++It)
	{
		const FProperty* Property = *It;
		check(Property);
		const FName PropertyName = Property->GetFName();

		if (UE::StateTree::PropertyRefHelpers::IsPropertyRef(*Property))
		{
			TArray<FStateTreePropertyPathIndirection> TargetIndirections;
			FStateTreePropertyPath TargetPath(TargetStruct.ID, PropertyName);
			if (!TargetPath.ResolveIndirectionsWithValue(TargetValue, TargetIndirections))
			{
				Log.Reportf(EMessageSeverity::Error, TargetStruct,
						TEXT("Couldn't resolve path to '%s' for target %s."),
						*PropertyName.ToString(), *TargetStruct.ToString());
				bSucceeded = false;
				continue;
			}
			else
			{
				const void* PropertyRef = TargetIndirections.Last().GetPropertyAddress();
				const bool bIsOptional = UE::StateTree::PropertyRefHelpers::IsPropertyRefMarkedAsOptional(*Property, PropertyRef);

				if (bIsOptional == false && !IsPropertyBound(PropertyName, OutReferenceBindings))
				{
					Log.Reportf(EMessageSeverity::Error, TargetStruct,
						TEXT("Property reference '%s' on % s is expected to have a binding."),
						*PropertyName.ToString(), *TargetStruct.ToString());
					bSucceeded = false;
					continue;
				}
			}
		}
		else
		{
			const bool bIsOptional = UE::StateTree::PropertyHelpers::HasOptionalMetadata(*Property);
			const EStateTreePropertyUsage Usage = UE::StateTree::GetUsageFromMetaData(Property);
			if (Usage == EStateTreePropertyUsage::Input)
			{
				// Make sure that an Input property is bound unless marked optional.
				if (bIsOptional == false && !IsPropertyBound(PropertyName, OutCopyBindings))
				{
					Log.Reportf(EMessageSeverity::Error, TargetStruct,
						TEXT("Input property '%s' on %s is expected to have a binding."),
						*PropertyName.ToString(), *TargetStruct.ToString());
					bSucceeded = false;
					continue;
				}
			}
			else if (Usage == EStateTreePropertyUsage::Context)
			{
				// Make sure that an Context property is manually or automatically bound. 
				const UStruct* ContextObjectType = nullptr; 
				if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
				{
					ContextObjectType = StructProperty->Struct;
				}		
				else if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
				{
					ContextObjectType = ObjectProperty->PropertyClass;
				}

				if (ContextObjectType == nullptr)
				{
					Log.Reportf(EMessageSeverity::Error, TargetStruct,
						TEXT("The type of Context property '%s' on %s is expected to be Object Reference or Struct."),
						*PropertyName.ToString(), *TargetStruct.ToString());
					bSucceeded = false;
					continue;
				}

				const bool bIsBound = IsPropertyBound(PropertyName, OutCopyBindings);

				if (!bIsBound)
				{
					const FStateTreeBindableStructDesc Desc = EditorData->FindContextData(ContextObjectType, PropertyName.ToString());

					if (Desc.IsValid())
					{
						// Add automatic binding to Context data.
						OutCopyBindings.Emplace(FStateTreePropertyPath(Desc.ID), FStateTreePropertyPath(TargetStruct.ID, PropertyName));
					}
					else
					{
						Log.Reportf(EMessageSeverity::Error, TargetStruct,
							TEXT("Could not find matching Context object for Context property '%s' on '%s'. Property must have manual binding."),
							*PropertyName.ToString(), *TargetStruct.ToString());
						bSucceeded = false;
						continue;
					}
				}
			}
		}
	}

	return bSucceeded;
}

void FStateTreeCompiler::InstantiateStructSubobjects(FStructView Struct)
{
	check(StateTree);
	check(EditorData);
	
	// Empty struct, nothing to do.
	if (!Struct.IsValid())
	{
		return;
	}

	for (TPropertyValueIterator<FProperty> It(Struct.GetScriptStruct(), Struct.GetMemory()); It; ++It)
	{
		if (const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(It->Key))
		{
			// Duplicate instanced objects.
			if (ObjectProperty->HasAnyPropertyFlags(CPF_InstancedReference))
			{
				if (UObject* Object = ObjectProperty->GetObjectPropertyValue(It->Value))
				{
					UObject* OuterObject = Object->GetOuter();
					// If the instanced object was created as Editor Data as outer,
					// change the outer to State Tree to prevent references to editor only data.
					if (Object->IsInOuter(EditorData))
					{
						OuterObject = StateTree;
					}
					UObject* DuplicatedObject = DuplicateObject(Object, OuterObject);
					ObjectProperty->SetObjectPropertyValue(const_cast<void*>(It->Value), DuplicatedObject);
				}
			}
		}
		if (const FStructProperty* StructProperty = CastField<FStructProperty>(It->Key))
		{
			// If we encounter instanced struct, recursively handle it too.
			if (StructProperty->Struct == TBaseStructure<FInstancedStruct>::Get())
			{
				FInstancedStruct& InstancedStruct = *static_cast<FInstancedStruct*>(const_cast<void*>(It->Value));
				InstantiateStructSubobjects(InstancedStruct);
			}
		}
	}
}
