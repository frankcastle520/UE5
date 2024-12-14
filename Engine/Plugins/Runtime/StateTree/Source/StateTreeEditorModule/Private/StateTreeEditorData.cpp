// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeEditorData.h"
#include "StateTree.h"
#include "StateTreeConditionBase.h"
#include "StateTreeConsiderationBase.h"
#include "StateTreeDelegates.h"
#include "StateTreeEditorModule.h"
#include "StateTreeEvaluatorBase.h"
#include "StateTreeNodeClassCache.h"
#include "StateTreePropertyFunctionBase.h"
#include "StateTreePropertyHelpers.h"
#include "StateTreeTaskBase.h"
#include "Algo/LevenshteinDistance.h"
#include "Customizations/StateTreeEditorNodeUtils.h"
#include "Modules/ModuleManager.h"

#if WITH_EDITOR
#include "StructUtilsDelegates.h"
#include "StructUtils/UserDefinedStruct.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(StateTreeEditorData)

#define LOCTEXT_NAMESPACE "StateTreeEditor"

namespace UE::StateTree::Editor
{
	const FString GlobalStateName(TEXT("Global"));
	const FString PropertyFunctionStateName(TEXT("Property Functions"));
	const FName ParametersNodeName(TEXT("Parameters"));

	bool IsPropertyFunctionOwnedByNode(FGuid NodeID, FGuid PropertyFuncID, const FStateTreeEditorPropertyBindings& EditorBindings)
	{
		for (const FStateTreePropertyPathBinding& Binding : EditorBindings.GetBindings())
		{
			const FGuid TargetID = Binding.GetTargetPath().GetStructID();			
			if (TargetID == NodeID)
			{
				return true;
			}

			FConstStructView NodeView = Binding.GetPropertyFunctionNode();
			if (const FStateTreeEditorNode* Node = NodeView.GetPtr<const FStateTreeEditorNode>())
			{
				if (Node->ID == PropertyFuncID)
				{
					PropertyFuncID = TargetID;
				}
			}
		}

		return false;
	}
}

UStateTreeEditorData::UStateTreeEditorData()
{
	FStateTreeEditorColor DefaultColor;
	DefaultColor.ColorRef = FStateTreeEditorColorRef();
	DefaultColor.Color = FLinearColor(FColor(31, 151, 167));
	DefaultColor.DisplayName = TEXT("Default Color");

	Colors.Add(MoveTemp(DefaultColor));

	EditorBindings.SetBindingsOwner(this);
}

void UStateTreeEditorData::PostInitProperties()
{
	Super::PostInitProperties();

	RootParameters.ID = FGuid::NewGuid();

#if WITH_EDITOR
	OnObjectsReinstancedHandle = FCoreUObjectDelegates::OnObjectsReinstanced.AddUObject(this, &UStateTreeEditorData::OnObjectsReinstanced);
	OnUserDefinedStructReinstancedHandle = UE::StructUtils::Delegates::OnUserDefinedStructReinstanced.AddUObject(this, &UStateTreeEditorData::OnUserDefinedStructReinstanced);
	OnParametersChangedHandle = UE::StateTree::Delegates::OnParametersChanged.AddUObject(this, &UStateTreeEditorData::OnParametersChanged);
	OnStateParametersChangedHandle = UE::StateTree::Delegates::OnStateParametersChanged.AddUObject(this, &UStateTreeEditorData::OnStateParametersChanged);
#endif
}

#if WITH_EDITOR

void UStateTreeEditorData::BeginDestroy()
{
	if (OnObjectsReinstancedHandle.IsValid())
	{
		FCoreUObjectDelegates::OnObjectsReinstanced.Remove(OnObjectsReinstancedHandle);
		OnObjectsReinstancedHandle.Reset();
	}
	if (OnUserDefinedStructReinstancedHandle.IsValid())
	{
		UE::StructUtils::Delegates::OnUserDefinedStructReinstanced.Remove(OnUserDefinedStructReinstancedHandle);
		OnUserDefinedStructReinstancedHandle.Reset();
	}
	if (OnParametersChangedHandle.IsValid())
	{
		UE::StateTree::Delegates::OnParametersChanged.Remove(OnParametersChangedHandle);
		OnParametersChangedHandle.Reset();
	}
	if (OnStateParametersChangedHandle.IsValid())
	{
		UE::StateTree::Delegates::OnStateParametersChanged.Remove(OnStateParametersChangedHandle);
		OnStateParametersChangedHandle.Reset();
	}
	
	Super::BeginDestroy();
}

void UStateTreeEditorData::OnObjectsReinstanced(const FReplacementObjectMap& ObjectMap)
{
	if (ObjectMap.IsEmpty())
	{
		return;
	}
	
	TSet<const UStruct*> Structs;
	for (TMap<UObject*, UObject*>::TConstIterator It(ObjectMap); It; ++It)
	{
		if (const UObject* ObjectToBeReplaced = It->Value)
		{
			Structs.Add(ObjectToBeReplaced->GetClass());
		}
	}

	bool bShouldUpdate = false;
	VisitAllNodes([&Structs, &bShouldUpdate](const UStateTreeState* State, const FStateTreeBindableStructDesc& Desc, const FStateTreeDataView Value)
	{
		if (Structs.Contains(Value.GetStruct()))
		{
			bShouldUpdate = true;
			return EStateTreeVisitor::Break; 
		}
		return EStateTreeVisitor::Continue;
	});

	if (!bShouldUpdate)
	{
		bShouldUpdate = EditorBindings.ContainsAnyStruct(Structs);
	}
	
	if (bShouldUpdate)
	{
		UpdateBindingsInstanceStructs();
	}
}

void UStateTreeEditorData::OnUserDefinedStructReinstanced(const UUserDefinedStruct& UserDefinedStruct)
{
	TSet<const UStruct*> Structs;
	Structs.Add(&UserDefinedStruct);

	bool bShouldUpdate = false;
	VisitAllNodes([&Structs, &bShouldUpdate](const UStateTreeState* State, const FStateTreeBindableStructDesc& Desc, const FStateTreeDataView Value)
	{
		if (Structs.Contains(Value.GetStruct()))
		{
			bShouldUpdate = true;
			return EStateTreeVisitor::Break; 
		}
		return EStateTreeVisitor::Continue;
	});

	if (!bShouldUpdate)
	{
		bShouldUpdate = EditorBindings.ContainsAnyStruct(Structs);
	}
	
	if (bShouldUpdate)
	{
		UpdateBindingsInstanceStructs();
	}
}

void UStateTreeEditorData::OnParametersChanged(const UStateTree& StateTree)
{
	if (const UStateTree* OwnerStateTree = GetTypedOuter<UStateTree>())
	{
		if (OwnerStateTree == &StateTree)
		{
			UpdateBindingsInstanceStructs();
		}
	}
}

void UStateTreeEditorData::OnStateParametersChanged(const UStateTree& StateTree, const FGuid StateID)
{
	if (const UStateTree* OwnerStateTree = GetTypedOuter<UStateTree>())
	{
		if (OwnerStateTree == &StateTree)
		{
			UpdateBindingsInstanceStructs();
		}
	}
}

void UStateTreeEditorData::PostLoad()
{
	Super::PostLoad();

	// Ensure the schema and states have had their PostLoad() fixed applied as we may need them in the later calls (or StateTree compile which might be calling this).
	if (Schema)
	{
		Schema->ConditionalPostLoad();
	}
	VisitHierarchy([](UStateTreeState& State, UStateTreeState* ParentState) mutable 
	{
		State.ConditionalPostLoad();
		return EStateTreeVisitor::Continue;
	});

	ReparentStates();
	FixObjectNodes();
	FixDuplicateIDs();
	UpdateBindingsInstanceStructs();
	CallPostLoadOnNodes();
}

void UStateTreeEditorData::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);

	FProperty* Property = PropertyChangedEvent.Property;
	FProperty* MemberProperty = nullptr;
	if (PropertyChangedEvent.PropertyChain.GetActiveMemberNode())
	{
		MemberProperty = PropertyChangedEvent.PropertyChain.GetActiveMemberNode()->GetValue();
	}

	if (MemberProperty && Property)
	{
		const UStateTree* StateTree = GetTypedOuter<UStateTree>();
		checkf(StateTree, TEXT("UStateTreeEditorData should only be allocated within a UStateTree"));
		
		const FName MemberName = MemberProperty->GetFName();
		if (MemberName == GET_MEMBER_NAME_CHECKED(UStateTreeEditorData, Schema))
		{			
			UE::StateTree::Delegates::OnSchemaChanged.Broadcast(*StateTree);
		}
		else if (MemberName == GET_MEMBER_NAME_CHECKED(UStateTreeEditorData, RootParameters))
		{
			UE::StateTree::Delegates::OnParametersChanged.Broadcast(*StateTree);
		}

		// Ensure unique ID on duplicated items.
		if (PropertyChangedEvent.ChangeType == EPropertyChangeType::Duplicate)
		{
			if (MemberName == GET_MEMBER_NAME_CHECKED(UStateTreeEditorData, Evaluators))
			{
				const int32 ArrayIndex = PropertyChangedEvent.GetArrayIndex(MemberProperty->GetFName().ToString());
				if (Evaluators.IsValidIndex(ArrayIndex))
				{
					const FGuid OldStructID = Evaluators[ArrayIndex].ID;
					Evaluators[ArrayIndex].ID = FGuid::NewGuid();
					EditorBindings.CopyBindings(OldStructID, Evaluators[ArrayIndex].ID);
				}
			}
			else if (MemberName == GET_MEMBER_NAME_CHECKED(UStateTreeEditorData, GlobalTasks))
			{
				const int32 ArrayIndex = PropertyChangedEvent.GetArrayIndex(MemberProperty->GetFName().ToString());
				if (GlobalTasks.IsValidIndex(ArrayIndex))
				{
					const FGuid OldStructID = GlobalTasks[ArrayIndex].ID;
					GlobalTasks[ArrayIndex].ID = FGuid::NewGuid();
					EditorBindings.CopyBindings(OldStructID, GlobalTasks[ArrayIndex].ID);
				}
			}
		}
		else if (PropertyChangedEvent.ChangeType == EPropertyChangeType::ArrayRemove)
		{
			if (MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UStateTreeEditorData, Evaluators)
				|| MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UStateTreeEditorData, GlobalTasks))
			{
				TMap<FGuid, const FStateTreeDataView> AllStructValues;
				GetAllStructValues(AllStructValues);
				Modify();
				EditorBindings.RemoveUnusedBindings(AllStructValues);
			}
		}

		// Notify that the global data changed (will need to update binding widgets, etc)
		if (MemberName == GET_MEMBER_NAME_CHECKED(UStateTreeEditorData, Evaluators)
			|| MemberName == GET_MEMBER_NAME_CHECKED(UStateTreeEditorData, GlobalTasks))
		{
			UE::StateTree::Delegates::OnGlobalDataChanged.Broadcast(*StateTree);
		}

		// Notify that the color data has changed
		if (MemberName == GET_MEMBER_NAME_CHECKED(UStateTreeEditorData, Colors))
		{
			UE::StateTree::Delegates::OnVisualThemeChanged.Broadcast(*StateTree);
		}
	}

	UE::StateTree::PropertyHelpers::DispatchPostEditToNodes(*this, PropertyChangedEvent, *this);
}
#endif // WITH_EDITOR

void UStateTreeEditorData::GetAccessibleStructs(const FGuid TargetStructID, TArray<FStateTreeBindableStructDesc>& OutStructDescs) const
{
	// Find the states that are updated before the current state.
	TArray<const UStateTreeState*> Path;
	const UStateTreeState* State = GetStateByStructID(TargetStructID);
	while (State != nullptr)
	{
		Path.Insert(State, 0);

		// Stop at subtree root.
		if (State->Type == EStateTreeStateType::Subtree)
		{
			break;
		}

		State = State->Parent;
	}
	
	GetAccessibleStructs(Path, TargetStructID, OutStructDescs);
}

void UStateTreeEditorData::GetAccessibleStructs(const TConstArrayView<const UStateTreeState*> Path, const FGuid TargetStructID, TArray<FStateTreeBindableStructDesc>& OutStructDescs) const
{
	const UStateTree* StateTree = GetTypedOuter<UStateTree>();
	checkf(StateTree, TEXT("UStateTreeEditorData should only be allocated within a UStateTree"));

	FStateTreeBindableStructDesc TargetStructDesc;
	bool bIsTargetPropertyFunction = false;
	if (GetStructByID(TargetStructID, TargetStructDesc))
	{
		bIsTargetPropertyFunction = TargetStructDesc.DataSource == EStateTreeBindableStructSource::PropertyFunction;
	}

	EStateTreeVisitor BaseProgress = VisitGlobalNodes([&OutStructDescs, TargetStructID]
		(const UStateTreeState* State, const FStateTreeBindableStructDesc& Desc, const FStateTreeDataView Value)
	{
		if (Desc.ID == TargetStructID)
		{
			return EStateTreeVisitor::Break;
		}
		
		OutStructDescs.Add(Desc);
		
		return EStateTreeVisitor::Continue;
	});

	if (BaseProgress == EStateTreeVisitor::Continue)
	{
		TArray<FStateTreeBindableStructDesc, TInlineAllocator<32>> BindableDescs;

		for (const UStateTreeState* State : Path)
		{
			if (State == nullptr)
			{
				continue;
			}
			
			const EStateTreeVisitor StateProgress = VisitStateNodes(*State, [&OutStructDescs, &BindableDescs, &Path, TargetStructID, bIsTargetPropertyFunction, this]
				(const UStateTreeState* State, const FStateTreeBindableStructDesc& Desc, const FStateTreeDataView Value)
				{
					// Stop iterating as soon as we find the target node.
					if (Desc.ID == TargetStructID)
					{
						OutStructDescs.Append(BindableDescs);
						return EStateTreeVisitor::Break;
					}

					// Not at target yet, collect all bindable source accessible so far.
					switch (Desc.DataSource)
					{
						case EStateTreeBindableStructSource::StateParameter:
						case EStateTreeBindableStructSource::Task:
						case EStateTreeBindableStructSource::StateEvent:
							BindableDescs.Add(Desc);
							break;

						case EStateTreeBindableStructSource::TransitionEvent:
						{
							// Checking if BindableStruct's owning Transition contains the Target.
							if (State == Path.Last())
							{
								for (const FStateTreeTransition& Transition : State->Transitions)
								{
									bool bFoundOwningTransition = false;
									for (const FStateTreeEditorNode& ConditionNode : Transition.Conditions)
									{
										if (ConditionNode.ID == TargetStructID
											|| (bIsTargetPropertyFunction && UE::StateTree::Editor::IsPropertyFunctionOwnedByNode(ConditionNode.ID, TargetStructID, EditorBindings)))
										{
											if (Transition.GetEventID() == Desc.ID)
											{
												BindableDescs.Add(Desc);
											}

											bFoundOwningTransition = true;
											break;
										}
									}

									if (bFoundOwningTransition)
									{
										break;
									}
								}
							}
							break;
						}

						case EStateTreeBindableStructSource::PropertyFunction:
						{
							if (State == Path.Last())
							{
								if (UE::StateTree::Editor::IsPropertyFunctionOwnedByNode(TargetStructID, Desc.ID, EditorBindings))
								{
									BindableDescs.Add(Desc);
								}
							}
						}
					}
							
					return EStateTreeVisitor::Continue;
				});
			
			if (StateProgress == EStateTreeVisitor::Break)
			{
				break;
			}
		}
	}
}

FStateTreeBindableStructDesc UStateTreeEditorData::FindContextData(const UStruct* ObjectType, const FString ObjectNameHint) const
{
	if (Schema == nullptr)
	{
		return FStateTreeBindableStructDesc();
	}

	// Find candidates based on type.
	TArray<FStateTreeBindableStructDesc> Candidates;
	for (const FStateTreeExternalDataDesc& Desc : Schema->GetContextDataDescs())
	{
		if (Desc.Struct->IsChildOf(ObjectType))
		{
			Candidates.Emplace(UE::StateTree::Editor::GlobalStateName, Desc.Name, Desc.Struct, FStateTreeDataHandle(), EStateTreeBindableStructSource::Context, Desc.ID);
		}
	}

	// Handle trivial cases.
	if (Candidates.IsEmpty())
	{
		return FStateTreeBindableStructDesc();
	}

	if (Candidates.Num() == 1)
	{
		return Candidates[0];
	}
	
	check(!Candidates.IsEmpty());
	
	// Multiple candidates, pick one that is closest match based on name.
	auto CalculateScore = [](const FString& Name, const FString& CandidateName)
	{
		if (CandidateName.IsEmpty())
		{
			return 1.0f;
		}
		const float WorstCase = static_cast<float>(Name.Len() + CandidateName.Len());
		return 1.0f - (static_cast<float>(Algo::LevenshteinDistance(Name, CandidateName)) / WorstCase);
	};
	
	const FString ObjectNameLowerCase = ObjectNameHint.ToLower();
	
	int32 HighestScoreIndex = 0;
	float HighestScore = CalculateScore(ObjectNameLowerCase, Candidates[0].Name.ToString().ToLower());
	
	for (int32 Index = 1; Index < Candidates.Num(); Index++)
	{
		const float Score = CalculateScore(ObjectNameLowerCase, Candidates[Index].Name.ToString().ToLower());
		if (Score > HighestScore)
		{
			HighestScore = Score;
			HighestScoreIndex = Index;
		}
	}
	
	return Candidates[HighestScoreIndex];
}

EStateTreeVisitor UStateTreeEditorData::EnumerateBindablePropertyFunctionNodes(TFunctionRef<EStateTreeVisitor(const UScriptStruct* NodeStruct, const FStateTreeBindableStructDesc& Desc, const FStateTreeDataView Value)> InFunc) const
{
	if (Schema == nullptr)
	{
		return EStateTreeVisitor::Continue;
	}

	FStateTreeEditorModule& EditorModule = FModuleManager::GetModuleChecked<FStateTreeEditorModule>(TEXT("StateTreeEditorModule"));
	FStateTreeNodeClassCache* ClassCache = EditorModule.GetNodeClassCache().Get();
	check(ClassCache);

	TArray<TSharedPtr<FStateTreeNodeClassData>> StructNodes;	
	ClassCache->GetStructs(FStateTreePropertyFunctionBase::StaticStruct(), StructNodes);
	for (const TSharedPtr<FStateTreeNodeClassData>& NodeClassData : StructNodes)
	{
		if (const UScriptStruct* NodeStruct = NodeClassData->GetScriptStruct())
		{
			if (NodeStruct == FStateTreePropertyFunctionBase::StaticStruct() || NodeStruct->HasMetaData(TEXT("Hidden")))
			{
				continue;
			}

			if (Schema->IsStructAllowed(NodeStruct))
			{
				if (const UStruct* InstanceDataStruct = NodeClassData->GetInstanceDataStruct())
				{
					FStateTreeBindableStructDesc Desc;
					Desc.Struct = InstanceDataStruct;
					Desc.ID = FGuid::NewDeterministicGuid(NodeStruct->GetName());
					Desc.DataSource = EStateTreeBindableStructSource::PropertyFunction;
					Desc.Name = FName(NodeStruct->GetDisplayNameText().ToString());
					Desc.StatePath = UE::StateTree::Editor::PropertyFunctionStateName;
					Desc.Category = NodeStruct->GetMetaData(TEXT("Category"));

					if (InFunc(NodeStruct, Desc, FStateTreeDataView(InstanceDataStruct, nullptr)) == EStateTreeVisitor::Break)
					{
						return EStateTreeVisitor::Break;
					}
				}
			}
		}
	}

	return EStateTreeVisitor::Continue;
}

bool UStateTreeEditorData::CanCreateParameter(const FGuid StructID) const
{
	if (RootParameters.ID == StructID)
	{
		return true;
	}

	bool bFoundStructID = false;

	VisitHierarchy([&StructID, &bFoundStructID](UStateTreeState& State, UStateTreeState* ParentState)->EStateTreeVisitor
	{
		if (State.Parameters.ID == StructID)
		{
			bFoundStructID = true;
			return EStateTreeVisitor::Break;
		}
		return EStateTreeVisitor::Continue;
	});

	return bFoundStructID;
}

void UStateTreeEditorData::CreateParameters(const FGuid StructID, TArrayView<FStateTreeEditorPropertyCreationDesc> InOutCreationDescs)
{
	if (InOutCreationDescs.IsEmpty())
	{
		return;
	}

	auto CreateProperties = [&InOutCreationDescs](FInstancedPropertyBag& PropertyBag)
	{
		TArray<FPropertyBagPropertyDesc, TInlineAllocator<1>> PropertyDescs;
		PropertyDescs.Reserve(InOutCreationDescs.Num());

		// Generate unique names for the incoming property descs to avoid changing the existing properties in the bag
		for (FStateTreeEditorPropertyCreationDesc& CreationDesc : InOutCreationDescs)
		{
			int32 Index = CreationDesc.PropertyDesc.Name.GetNumber();
			while (PropertyBag.FindPropertyDescByName(CreationDesc.PropertyDesc.Name))
			{
				CreationDesc.PropertyDesc.Name = FName(CreationDesc.PropertyDesc.Name, Index++);
			}
			PropertyDescs.Add(CreationDesc.PropertyDesc);
		}

		PropertyBag.AddProperties(PropertyDescs);

		for (const FStateTreeEditorPropertyCreationDesc& CreationDesc : InOutCreationDescs)
		{
			// Attempt to copy the value from the Source Property / Addr to Property Desc
			// There could be Type Mismatches if the Descs don't match the Source Property, but attempt to do it on all property descs
			if (CreationDesc.SourceProperty && CreationDesc.SourceContainerAddress)
			{
				PropertyBag.SetValue(CreationDesc.PropertyDesc.Name, CreationDesc.SourceProperty, CreationDesc.SourceContainerAddress);
			}
		}
	};

	const UStateTree* StateTree = GetTypedOuter<UStateTree>();
	checkf(StateTree, TEXT("UStateTreeEditorData should only be allocated within a UStateTree"));

	if (RootParameters.ID == StructID)
	{
		CreateProperties(RootParameters.Parameters);
		UE::StateTree::Delegates::OnParametersChanged.Broadcast(*StateTree);
		return;
	}

	VisitHierarchy([&StructID, &CreateProperties, StateTree](UStateTreeState& State, UStateTreeState* ParentState)->EStateTreeVisitor
	{
		if (State.Parameters.ID == StructID)
		{
			CreateProperties(State.Parameters.Parameters);
			UE::StateTree::Delegates::OnStateParametersChanged.Broadcast(*StateTree, State.ID);
			return EStateTreeVisitor::Break;
		}
		return EStateTreeVisitor::Continue;
	});
}

bool UStateTreeEditorData::GetStructByID(const FGuid StructID, FStateTreeBindableStructDesc& OutStructDesc) const
{
	VisitAllNodes([&OutStructDesc, StructID](const UStateTreeState* State, const FStateTreeBindableStructDesc& Desc, const FStateTreeDataView Value)
	{
		if (Desc.ID == StructID)
		{
			OutStructDesc = Desc;
			return EStateTreeVisitor::Break;
		}
		return EStateTreeVisitor::Continue;
	});
	
	return OutStructDesc.IsValid();
}

bool UStateTreeEditorData::GetDataViewByID(const FGuid StructID, FStateTreeDataView& OutDataView) const
{
	bool bFound = false;
	VisitAllNodes([&OutDataView, &bFound, StructID](const UStateTreeState* State, const FStateTreeBindableStructDesc& Desc, const FStateTreeDataView Value)
	{
		if (Desc.ID == StructID)
		{
			bFound = true;
			OutDataView = Value;
			return EStateTreeVisitor::Break;
		}
		return EStateTreeVisitor::Continue;
	});

	return bFound;
}

const UStateTreeState* UStateTreeEditorData::GetStateByStructID(const FGuid TargetStructID) const
{
	const UStateTreeState* Result = nullptr;

	VisitHierarchyNodes([&Result, TargetStructID](const UStateTreeState* State, const FStateTreeBindableStructDesc& Desc, const FStateTreeDataView Value)
		{
			if (Desc.ID == TargetStructID)
			{
				Result = State;
				return EStateTreeVisitor::Break;
			}
			return EStateTreeVisitor::Continue;
			
		});

	return Result;
}

const UStateTreeState* UStateTreeEditorData::GetStateByID(const FGuid StateID) const
{
	return const_cast<UStateTreeEditorData*>(this)->GetMutableStateByID(StateID);
}

UStateTreeState* UStateTreeEditorData::GetMutableStateByID(const FGuid StateID)
{
	UStateTreeState* Result = nullptr;
	
	VisitHierarchy([&Result, &StateID](UStateTreeState& State, UStateTreeState* /*ParentState*/)
	{
		if (State.ID == StateID)
		{
			Result = &State;
			return EStateTreeVisitor::Break;
		}

		return EStateTreeVisitor::Continue;
	});

	return Result;
}

void UStateTreeEditorData::GetAllStructIDs(TMap<FGuid, const UStruct*>& AllStructs) const
{
	AllStructs.Reset();

	const UStateTree* StateTree = GetTypedOuter<UStateTree>();
	checkf(StateTree, TEXT("UStateTreeEditorData should only be allocated within a UStateTree"));

	VisitAllNodes([&AllStructs](const UStateTreeState* State, const FStateTreeBindableStructDesc& Desc, const FStateTreeDataView Value)
		{
			AllStructs.Emplace(Desc.ID, Desc.Struct);
			return EStateTreeVisitor::Continue;
		});
}

void UStateTreeEditorData::GetAllStructValues(TMap<FGuid, const FStateTreeDataView>& AllValues) const
{
	AllValues.Reset();

	const UStateTree* StateTree = GetTypedOuter<UStateTree>();
	checkf(StateTree, TEXT("UStateTreeEditorData should only be allocated within a UStateTree"));

	VisitAllNodes([&AllValues](const UStateTreeState* State, const FStateTreeBindableStructDesc& Desc, const FStateTreeDataView Value)
		{
			AllValues.Emplace(Desc.ID, Value);
			return EStateTreeVisitor::Continue;
		});
}

void UStateTreeEditorData::ReparentStates()
{
	VisitHierarchy([TreeData = this](UStateTreeState& State, UStateTreeState* ParentState) mutable 
	{
		UObject* ExpectedOuter = ParentState ? Cast<UObject>(ParentState) : Cast<UObject>(TreeData);
		if (State.GetOuter() != ExpectedOuter)
		{
			UE_LOG(LogStateTreeEditor, Log, TEXT("%s: Fixing outer on state %s."), *TreeData->GetFullName(), *GetNameSafe(&State));
			State.Rename(nullptr, ExpectedOuter, REN_DontCreateRedirectors | REN_DoNotDirty);
		}
		
		State.Parent = ParentState;
		
		return EStateTreeVisitor::Continue;
	});
}

void UStateTreeEditorData::FixObjectInstance(TSet<UObject*>& SeenObjects, UObject& Outer, FStateTreeEditorNode& Node)
{
	if (Node.InstanceObject)
	{
		// Found a duplicate reference to an object, make unique copy.
		if (SeenObjects.Contains(Node.InstanceObject))
		{
			UE_LOG(LogStateTreeEditor, Log, TEXT("%s: Making duplicate node instance %s unique."), *GetFullName(), *GetNameSafe(Node.InstanceObject));
			Node.InstanceObject = DuplicateObject(Node.InstanceObject, &Outer);
		}
		else
		{
			// Make sure the instance object is property outered.
			if (Node.InstanceObject->GetOuter() != &Outer)
			{
				UE_LOG(LogStateTreeEditor, Log, TEXT("%s: Fixing outer on node instance %s."), *GetFullName(), *GetNameSafe(Node.InstanceObject));
				Node.InstanceObject->Rename(nullptr, &Outer, REN_DontCreateRedirectors | REN_DoNotDirty);
			}
		}
		SeenObjects.Add(Node.InstanceObject);
	}
};

void UStateTreeEditorData::FixObjectNodes()
{
	// Older version of State Trees had all instances outered to the editor data. This causes issues with State copy/paste.
	// Instance data does not get duplicated but the copied state will reference the object on the source state instead.
	//
	// Ensure that all node objects are parented to their states, and make duplicated instances unique.

	TSet<UObject*> SeenObjects;
	
	VisitHierarchy([&SeenObjects, TreeData = this](UStateTreeState& State, UStateTreeState* ParentState) mutable 
	{

		// Enter conditions
		for (FStateTreeEditorNode& Node : State.EnterConditions)
		{
			TreeData->FixObjectInstance(SeenObjects, State, Node);
		}
		
		// Tasks
		for (FStateTreeEditorNode& Node : State.Tasks)
		{
			TreeData->FixObjectInstance(SeenObjects, State, Node);
		}

		TreeData->FixObjectInstance(SeenObjects, State, State.SingleTask);


		// Transitions
		for (FStateTreeTransition& Transition : State.Transitions)
		{
			for (FStateTreeEditorNode& Node : Transition.Conditions)
			{
				TreeData->FixObjectInstance(SeenObjects, State, Node);
			}
		}
		
		return EStateTreeVisitor::Continue;
	});

	for (FStateTreeEditorNode& Node : Evaluators)
	{
		FixObjectInstance(SeenObjects, *this, Node);
	}

	for (FStateTreeEditorNode& Node : GlobalTasks)
	{
		FixObjectInstance(SeenObjects, *this, Node);
	}
}

FText UStateTreeEditorData::GetNodeDescription(const FStateTreeEditorNode& Node, const EStateTreeNodeFormatting Formatting) const
{
	if (const FStateTreeNodeBase* NodePtr = Node.Node.GetPtr<FStateTreeNodeBase>())
	{
		// If the node has name override, return it.
		if (!NodePtr->Name.IsNone())
		{
			return FText::FromName(NodePtr->Name);
		}

		// If the node has automatic description, return it.
		const FStateTreeBindingLookup BindingLookup(this);
		const FStateTreeDataView InstanceData = Node.GetInstance();
		if (InstanceData.IsValid())
		{
			
			const FText Description = NodePtr->GetDescription(Node.ID, InstanceData, BindingLookup, Formatting);
			if (!Description.IsEmpty())
			{
				return Description;
			}
		}

		// As last resort, return node's display name.
		check(Node.Node.GetScriptStruct());
		return Node.Node.GetScriptStruct()->GetDisplayNameText();
	}

	// The node is not initialized.
	return LOCTEXT("EmptyNode", "None");
}

void UStateTreeEditorData::FixDuplicateIDs()
{
	// Around version 5.1-5.3 we had issue that copy/paste or some duplication methods could create nodes with duplicate IDs.
	// This code tries to fix that, it looks for duplicates, makes them unique, and duplicates the bindings when ID changes.
	TSet<FGuid> FoundNodeIDs;

	// Evaluators
	for (int32 Index = 0; Index < Evaluators.Num(); Index++)
	{
		FStateTreeEditorNode& Node = Evaluators[Index];
		if (const FStateTreeEvaluatorBase* Evaluator = Node.Node.GetPtr<FStateTreeEvaluatorBase>())
		{
			const FGuid OldID = Node.ID; 
			if (FoundNodeIDs.Contains(Node.ID))
			{
				Node.ID = UE::StateTree::PropertyHelpers::MakeDeterministicID(*this, TEXT("Evaluators"), Index);
				
				UE_LOG(LogStateTreeEditor, Log, TEXT("%s: Found Evaluator '%s' with duplicate ID, changing ID:%s to ID:%s."),
					*GetFullName(), *Node.GetName().ToString(), *OldID.ToString(), *Node.ID.ToString());
				EditorBindings.CopyBindings(OldID, Node.ID);
			}
			FoundNodeIDs.Add(Node.ID);
		}
	}
	
	// Global Tasks
	for (int32 Index = 0; Index < GlobalTasks.Num(); Index++)
	{
		FStateTreeEditorNode& Node = GlobalTasks[Index];
		if (const FStateTreeTaskBase* Task = Node.Node.GetPtr<FStateTreeTaskBase>())
		{
			const FGuid OldID = Node.ID; 
			if (FoundNodeIDs.Contains(Node.ID))
			{
				Node.ID = UE::StateTree::PropertyHelpers::MakeDeterministicID(*this, TEXT("GlobalTasks"), Index);
				
				UE_LOG(LogStateTreeEditor, Log, TEXT("%s: Found GlobalTask '%s' with duplicate ID, changing ID:%s to ID:%s."),
					*GetFullName(), *Node.GetName().ToString(), *OldID.ToString(), *Node.ID.ToString());
				EditorBindings.CopyBindings(OldID, Node.ID);
			}
			FoundNodeIDs.Add(Node.ID);
		}
	}
	
	VisitHierarchy([&FoundNodeIDs, &EditorBindings = EditorBindings, &Self = *this](UStateTreeState& State, UStateTreeState* ParentState)
	{
		// Enter conditions
		for (int32 Index = 0; Index < State.EnterConditions.Num(); Index++)
		{
			FStateTreeEditorNode& Node = State.EnterConditions[Index];
			if (const FStateTreeConditionBase* Cond = Node.Node.GetPtr<FStateTreeConditionBase>())
			{
				const FGuid OldID = Node.ID;
				
				bool bIsAlreadyInSet = false;
				FoundNodeIDs.Add(Node.ID, &bIsAlreadyInSet);
				if (bIsAlreadyInSet)
				{
					Node.ID = UE::StateTree::PropertyHelpers::MakeDeterministicID(State, TEXT("EnterConditions"), Index);
					
					UE_LOG(LogStateTreeEditor, Log, TEXT("%s: Found Enter Condition '%s' with duplicate ID on state '%s', changing ID:%s to ID:%s."),
						*Self.GetFullName(), *Node.GetName().ToString(), *GetNameSafe(&State), *OldID.ToString(), *Node.ID.ToString());
					EditorBindings.CopyBindings(OldID, Node.ID);
				}
			}
		}

		// Tasks
		for (int32 Index = 0; Index < State.Tasks.Num(); Index++)
		{
			FStateTreeEditorNode& Node = State.Tasks[Index];
			if (const FStateTreeTaskBase* Task = Node.Node.GetPtr<FStateTreeTaskBase>())
			{
				const FGuid OldID = Node.ID;
				
				bool bIsAlreadyInSet = false;
				FoundNodeIDs.Add(Node.ID, &bIsAlreadyInSet);
				if (bIsAlreadyInSet)
				{
					Node.ID = UE::StateTree::PropertyHelpers::MakeDeterministicID(State, TEXT("Tasks"), Index);

					UE_LOG(LogStateTreeEditor, Log, TEXT("%s: Found Task '%s' with duplicate ID on state '%s', changing ID:%s to ID:%s."),
						*Self.GetFullName(), *Node.GetName().ToString(), *GetNameSafe(&State), *OldID.ToString(), *Node.ID.ToString());
					EditorBindings.CopyBindings(OldID, Node.ID);
				}
			}
		}

		if (FStateTreeTaskBase* Task = State.SingleTask.Node.GetMutablePtr<FStateTreeTaskBase>())
		{
			const FGuid OldID = State.SingleTask.ID;

			bool bIsAlreadyInSet = false;
			FoundNodeIDs.Add(State.SingleTask.ID, &bIsAlreadyInSet);
			if (bIsAlreadyInSet)
			{
				State.SingleTask.ID = UE::StateTree::PropertyHelpers::MakeDeterministicID(State, TEXT("SingleTask"), 0);

				UE_LOG(LogStateTreeEditor, Log, TEXT("%s: Found enter condition '%s' with duplicate ID on state '%s', changing ID:%s to ID:%s."),
					*Self.GetFullName(), *State.SingleTask.GetName().ToString(), *GetNameSafe(&State), *OldID.ToString(), *State.SingleTask.ID.ToString());
				EditorBindings.CopyBindings(OldID, State.SingleTask.ID);
			}
		}

		// Transitions
		for (int32 TransitionIndex = 0; TransitionIndex < State.Transitions.Num(); TransitionIndex++)
		{
			FStateTreeTransition& Transition = State.Transitions[TransitionIndex];
			for (int32 Index = 0; Index < Transition.Conditions.Num(); Index++)
			{
				FStateTreeEditorNode& Node = Transition.Conditions[Index];
				if (const FStateTreeConditionBase* Cond = Node.Node.GetPtr<FStateTreeConditionBase>())
				{
					const FGuid OldID = Node.ID; 
					bool bIsAlreadyInSet = false;
					FoundNodeIDs.Add(Node.ID, &bIsAlreadyInSet);
					if (bIsAlreadyInSet)
					{
						Node.ID = UE::StateTree::PropertyHelpers::MakeDeterministicID(State, TEXT("TransitionConditions"), ((uint64)TransitionIndex << 32) | (uint64)Index);

						UE_LOG(LogStateTreeEditor, Log, TEXT("%s: Found transition condition '%s' with duplicate ID on state '%s', changing ID:%s to ID:%s."),
							*Self.GetFullName(), *Node.GetName().ToString(), *GetNameSafe(&State), *OldID.ToString(), *Node.ID.ToString());
						EditorBindings.CopyBindings(OldID, Node.ID);
					}
				}
			}
		}
		
		return EStateTreeVisitor::Continue;
	});

	// It is possible that the user has changed the node type so some of the bindings might not make sense anymore, clean them up.
	TMap<FGuid, const FStateTreeDataView> AllValues;
	GetAllStructValues(AllValues);
	EditorBindings.RemoveUnusedBindings(AllValues);
}

void UStateTreeEditorData::UpdateBindingsInstanceStructs()
{
	TMap<FGuid, const FStateTreeDataView> AllValues;
	GetAllStructValues(AllValues);
	for (FStateTreePropertyPathBinding& Binding : EditorBindings.GetMutableBindings())
	{
		if (AllValues.Contains(Binding.GetSourcePath().GetStructID()))
		{
			Binding.GetMutableSourcePath().UpdateSegmentsFromValue(AllValues[Binding.GetSourcePath().GetStructID()]);
		}

		if (AllValues.Contains(Binding.GetTargetPath().GetStructID()))
		{
			Binding.GetMutableTargetPath().UpdateSegmentsFromValue(AllValues[Binding.GetTargetPath().GetStructID()]);
		}
	}
}

void UStateTreeEditorData::CallPostLoadOnNodes()
{
	for (FStateTreeEditorNode& EvaluatorEditorNode : Evaluators)
	{
		if (FStateTreeNodeBase* EvaluatorNode = EvaluatorEditorNode.Node.GetMutablePtr<FStateTreeNodeBase>())
		{
			UE::StateTreeEditor::EditorNodeUtils::ConditionalUpdateNodeInstanceData(EvaluatorEditorNode, *this);
			EvaluatorNode->PostLoad(EvaluatorEditorNode.GetInstance());
		}
	}

	for (FStateTreeEditorNode& GlobalTaskEditorNode : GlobalTasks)
	{
		if (FStateTreeNodeBase* GlobalTaskNode = GlobalTaskEditorNode.Node.GetMutablePtr<FStateTreeNodeBase>())
		{
			UE::StateTreeEditor::EditorNodeUtils::ConditionalUpdateNodeInstanceData(GlobalTaskEditorNode, *this);
			GlobalTaskNode->PostLoad(GlobalTaskEditorNode.GetInstance());
		}
	}
}

EStateTreeVisitor UStateTreeEditorData::VisitStateNodes(const UStateTreeState& State, TFunctionRef<EStateTreeVisitor(const UStateTreeState* State, const FGuid& ID, const FName& Name, const EStateTreeNodeType NodeType, const UScriptStruct* NodeStruct, const UStruct* InstanceStruct)> InFunc) const
{
	bool bContinue = true;

	auto VisitFuncNodes = [&InFunc, &State, this](const FGuid StructID, const FName NodeName)
	{
		const FString StatePath = FString::Printf(TEXT("%s/%s"), *State.GetPath(), *NodeName.ToString());
		return VisitStructBoundPropertyFunctions(StructID, StatePath, [&InFunc, &State](const FStateTreeEditorNode& EditorNode, const FStateTreeBindableStructDesc& Desc, FStateTreeDataView Value)
		{
			return InFunc(&State, EditorNode.ID, EditorNode.GetName(), EStateTreeNodeType::PropertyFunction, EditorNode.Node.GetScriptStruct(), EditorNode.GetInstance().GetStruct());
		});
	};

	if (bContinue)
	{
		// Enter conditions
		for (const FStateTreeEditorNode& Node : State.EnterConditions)
		{
			if (VisitFuncNodes(Node.ID, Node.GetName()) == EStateTreeVisitor::Break)
			{
				bContinue = false;
				break;
			}
			else if (const FStateTreeConditionBase* Cond = Node.Node.GetPtr<FStateTreeConditionBase>())
			{
				if (InFunc(&State, Node.ID, Node.Node.GetScriptStruct()->GetFName(), EStateTreeNodeType::EnterCondition, Node.Node.GetScriptStruct(), Cond->GetInstanceDataType()) == EStateTreeVisitor::Break)
				{
					bContinue = false;
					break;
				}
			}
		}
	}
	if (bContinue)
	{
		// Tasks
		for (const FStateTreeEditorNode& Node : State.Tasks)
		{
			if (VisitFuncNodes(Node.ID, Node.GetName()) == EStateTreeVisitor::Break)
			{
				bContinue = false;
				break;
			}
			else if (const FStateTreeTaskBase* Task = Node.Node.GetPtr<FStateTreeTaskBase>())
			{
				if (InFunc(&State, Node.ID, Task->Name, EStateTreeNodeType::Task, Node.Node.GetScriptStruct(), Task->GetInstanceDataType()) == EStateTreeVisitor::Break)
				{
					bContinue = false;
					break;
				}
			}
		}
	}
	if (bContinue)
	{
		if (const FStateTreeTaskBase* Task = State.SingleTask.Node.GetPtr<FStateTreeTaskBase>())
		{
			if (VisitFuncNodes(State.SingleTask.ID, State.SingleTask.GetName()) == EStateTreeVisitor::Break)
			{
				bContinue = false;
			}
			else if (InFunc(&State, State.SingleTask.ID, Task->Name, EStateTreeNodeType::Task, State.SingleTask.Node.GetScriptStruct(), Task->GetInstanceDataType()) == EStateTreeVisitor::Break)
			{
				bContinue = false;
			}
		}

	}
	if (bContinue)
	{
		// Transitions
		for (const FStateTreeTransition& Transition : State.Transitions)
		{
			for (const FStateTreeEditorNode& Node : Transition.Conditions)
			{
				if (VisitFuncNodes(Node.ID, Node.GetName()) == EStateTreeVisitor::Break)
				{
					bContinue = false;
					break;
				}
				else if (const FStateTreeConditionBase* Cond = Node.Node.GetPtr<FStateTreeConditionBase>())
				{
					if (InFunc(&State, Node.ID, Node.Node.GetScriptStruct()->GetFName(), EStateTreeNodeType::TransitionCondition, Node.Node.GetScriptStruct(), Cond->GetInstanceDataType()) == EStateTreeVisitor::Break)
					{
						bContinue = false;
						break;
					}
				}
			}
		}
	}
	if (bContinue)
	{
		// Bindable state parameters
		if (State.Type != EStateTreeStateType::Subtree
			&& State.Parameters.Parameters.IsValid())
		{
			if (VisitFuncNodes(State.Parameters.ID, UE::StateTree::Editor::ParametersNodeName) == EStateTreeVisitor::Break)
			{
				bContinue = false;
			}
			else if (InFunc(&State, State.Parameters.ID, State.Name, EStateTreeNodeType::StateParameters, nullptr, State.Parameters.Parameters.GetPropertyBagStruct()) == EStateTreeVisitor::Break)
			{
				bContinue = false;
			}
		}
	}

	return bContinue ? EStateTreeVisitor::Continue : EStateTreeVisitor::Break;
}

EStateTreeVisitor UStateTreeEditorData::VisitStateNodes(const UStateTreeState& State, TFunctionRef<EStateTreeVisitor(const UStateTreeState* State, const FStateTreeBindableStructDesc& Desc, const FStateTreeDataView Value)> InFunc) const
{
	auto VisitFuncNodes = [&InFunc, &State, this](const FGuid StructID, const FName NodeName)
	{
		const FString StatePath = FString::Printf(TEXT("%s/%s"), *State.GetPath(), *NodeName.ToString());
		return VisitStructBoundPropertyFunctions(StructID, StatePath, [&InFunc, &State](const FStateTreeEditorNode& EditorNode, const FStateTreeBindableStructDesc& Desc, FStateTreeDataView Value)
		{
			return InFunc(&State, Desc, Value);
		});
	};

	bool bContinue = true;

	const FString StatePath = State.GetPath();
	
	if (bContinue)
	{
		// Bindable state parameters
		if (State.Parameters.Parameters.IsValid())
		{
			if (VisitFuncNodes(State.Parameters.ID, UE::StateTree::Editor::ParametersNodeName) == EStateTreeVisitor::Break)
			{
				bContinue = false;
			}
			else
			{
				FStateTreeBindableStructDesc Desc;
				Desc.StatePath = StatePath;
				Desc.Struct = State.Parameters.Parameters.GetPropertyBagStruct();
				Desc.Name = FName("Parameters");
				Desc.ID = State.Parameters.ID;
				Desc.DataSource = EStateTreeBindableStructSource::StateParameter;

				if (InFunc(&State, Desc, FStateTreeDataView(const_cast<FInstancedPropertyBag&>(State.Parameters.Parameters).GetMutableValue())) == EStateTreeVisitor::Break)
				{
					bContinue = false;
				}
			}
		}
	}

	const FString StatePathWithConditions = StatePath + TEXT("/EnterConditions");
	
	if (bContinue)
	{
		if (State.bHasRequiredEventToEnter )
		{
			FStateTreeBindableStructDesc Desc;
			Desc.StatePath = StatePathWithConditions;
			Desc.Struct = FStateTreeEvent::StaticStruct();
			Desc.Name = FName("Enter Event");
			Desc.ID = State.GetEventID();
			Desc.DataSource = EStateTreeBindableStructSource::StateEvent;

			if (InFunc(&State, Desc, FStateTreeDataView(FStructView::Make(const_cast<UStateTreeState&>(State).RequiredEventToEnter.GetTemporaryEvent()))) == EStateTreeVisitor::Break)
			{
				bContinue = false;
			}
		}
	}

	if (bContinue)
	{
		// Enter conditions
		for (const FStateTreeEditorNode& Node : State.EnterConditions)
		{
			if (VisitFuncNodes(Node.ID, Node.GetName()) == EStateTreeVisitor::Break)
			{
				bContinue = false;
				break;
			}
			else if (const FStateTreeConditionBase* Cond = Node.Node.GetPtr<FStateTreeConditionBase>())
			{
				FStateTreeBindableStructDesc Desc;
				Desc.StatePath = StatePathWithConditions;
				Desc.Struct = Cond->GetInstanceDataType();
				Desc.Name = Node.GetName();
				Desc.ID = Node.ID;
				Desc.DataSource = EStateTreeBindableStructSource::Condition;

				if (InFunc(&State, Desc, Node.GetInstance()) == EStateTreeVisitor::Break)
				{
					bContinue = false;
					break;
				}
			}
		}
	}
	if (bContinue)
	{
		const FString StatePathWithConsiderations = StatePath + TEXT("/Considerations");
		// Utility Considerations
		for (const FStateTreeEditorNode& Node : State.Considerations)
		{
			if (VisitFuncNodes(Node.ID, Node.GetName()) == EStateTreeVisitor::Break)
			{
				bContinue = false;
				break;
			}
			else if (const FStateTreeConsiderationBase* Consideration = Node.Node.GetPtr<FStateTreeConsiderationBase>())
			{
				FStateTreeBindableStructDesc Desc;
				Desc.StatePath = StatePathWithConsiderations;
				Desc.Struct = Consideration->GetInstanceDataType();
				Desc.Name = Node.GetName();
				Desc.ID = Node.ID;
				Desc.DataSource = EStateTreeBindableStructSource::Consideration;

				if (InFunc(&State, Desc, Node.GetInstance()) == EStateTreeVisitor::Break)
				{
					bContinue = false;
					break;
				}
			}
		}
	}
	if (bContinue)
	{
		// Tasks
		for (const FStateTreeEditorNode& Node : State.Tasks)
		{
			if (VisitFuncNodes(Node.ID, Node.GetName()) == EStateTreeVisitor::Break)
			{
				bContinue = false;
				break;
			}
			else if (const FStateTreeTaskBase* Task = Node.Node.GetPtr<FStateTreeTaskBase>())
			{
				FStateTreeBindableStructDesc Desc;
				Desc.StatePath = StatePath;
				Desc.Struct = Task->GetInstanceDataType();
				Desc.Name = Node.GetName();
				Desc.ID = Node.ID;
				Desc.DataSource = EStateTreeBindableStructSource::Task;

				if (InFunc(&State, Desc, Node.GetInstance()) == EStateTreeVisitor::Break)
				{
					bContinue = false;
					break;
				}
			}
		}
	}
	if (bContinue)
	{
		if (const FStateTreeTaskBase* Task = State.SingleTask.Node.GetPtr<FStateTreeTaskBase>())
		{
			if (VisitFuncNodes(State.SingleTask.ID, State.SingleTask.GetName()) == EStateTreeVisitor::Break)
			{
				bContinue = false;
			}
			else
			{
				FStateTreeBindableStructDesc Desc;
				Desc.StatePath = StatePath;
				Desc.Struct = Task->GetInstanceDataType();
				Desc.Name = State.SingleTask.GetName();
				Desc.ID = State.SingleTask.ID;
				Desc.DataSource = EStateTreeBindableStructSource::Task;

				if (InFunc(&State, Desc, State.SingleTask.GetInstance()) == EStateTreeVisitor::Break)
				{
					bContinue = false;
				}
			}
		}

	}
	if (bContinue)
	{
		// Transitions
		for (int32 TransitionIndex = 0; TransitionIndex < State.Transitions.Num(); TransitionIndex++)
		{
			const FStateTreeTransition& Transition = State.Transitions[TransitionIndex];
			const FString StatePathWithTransition = StatePath + FString::Printf(TEXT("/Transition[%d]"), TransitionIndex);

			if (Transition.Trigger == EStateTreeTransitionTrigger::OnEvent)
			{
				FStateTreeBindableStructDesc Desc;
				Desc.StatePath = StatePathWithTransition;
				Desc.Struct = FStateTreeEvent::StaticStruct();
				Desc.Name = FName(TEXT("Transition Event"));
				Desc.ID = Transition.GetEventID();
				Desc.DataSource = EStateTreeBindableStructSource::TransitionEvent;

				if (InFunc(&State, Desc, FStateTreeDataView(FStructView::Make(const_cast<FStateTreeTransition&>(Transition).RequiredEvent.GetTemporaryEvent()))) == EStateTreeVisitor::Break)
				{
					bContinue = false;
					break;
				}
			}

			for (const FStateTreeEditorNode& Node : Transition.Conditions)
			{
				if (VisitFuncNodes(Node.ID, Node.GetName()) == EStateTreeVisitor::Break)
				{
					bContinue = false;
					break;
				}
				else if (const FStateTreeConditionBase* Cond = Node.Node.GetPtr<FStateTreeConditionBase>())
				{
					FStateTreeBindableStructDesc Desc;
					Desc.StatePath = StatePathWithTransition;
					Desc.Struct = Cond->GetInstanceDataType();
					Desc.Name = Node.GetName();
					Desc.ID = Node.ID;
					Desc.DataSource = EStateTreeBindableStructSource::Condition;

					if (InFunc(&State, Desc, Node.GetInstance()) == EStateTreeVisitor::Break)
					{
						bContinue = false;
						break;
					}
				}
			}
		}
	}

	return bContinue ? EStateTreeVisitor::Continue : EStateTreeVisitor::Break;
}

EStateTreeVisitor UStateTreeEditorData::VisitStructBoundPropertyFunctions(FGuid StructID, const FString& StatePath, TFunctionRef<EStateTreeVisitor(const FStateTreeEditorNode& EditorNode, const FStateTreeBindableStructDesc& Desc, const FStateTreeDataView Value)> InFunc) const
{
	TArray<const FStateTreePropertyPathBinding*> Bindings;
	EditorBindings.GetPropertyBindingsFor(StructID, Bindings);

	for (const FStateTreePropertyPathBinding* Binding : Bindings)
	{
		const FConstStructView FunctionNodeView = Binding->GetPropertyFunctionNode();
		if (const FStateTreeEditorNode* FunctionNode = FunctionNodeView.GetPtr<const FStateTreeEditorNode>())
		{
			if (VisitStructBoundPropertyFunctions(FunctionNode->ID, StatePath, InFunc) == EStateTreeVisitor::Break)
			{
				return EStateTreeVisitor::Break;
			}

			FStateTreeBindableStructDesc Desc;
			Desc.Struct = FunctionNode->GetInstance().GetStruct();
			if (const UStruct* NodeStruct = FunctionNode->Node.GetScriptStruct())
			{
				Desc.ID = FunctionNode->ID;
				Desc.DataSource = EStateTreeBindableStructSource::PropertyFunction;
				Desc.Name = FName(NodeStruct->GetDisplayNameText().ToString());
				Desc.StatePath = FString::Printf(TEXT("%s/%s"), *StatePath, *UE::StateTree::Editor::PropertyFunctionStateName);

				if (InFunc(*FunctionNode, Desc, FunctionNode->GetInstance()) == EStateTreeVisitor::Break)
				{
					return EStateTreeVisitor::Break;
				}
			}
		}
	}

	return EStateTreeVisitor::Continue;
}

EStateTreeVisitor UStateTreeEditorData::VisitHierarchy(TFunctionRef<EStateTreeVisitor(UStateTreeState& State, UStateTreeState* ParentState)> InFunc) const
{
	using FStatePair = TTuple<UStateTreeState*, UStateTreeState*>; 
	TArray<FStatePair> Stack;
	bool bContinue = true;

	for (UStateTreeState* SubTree : SubTrees)
	{
		if (!SubTree)
		{
			continue;
		}

		Stack.Add( FStatePair(nullptr, SubTree));

		while (!Stack.IsEmpty() && bContinue)
		{
			FStatePair Current = Stack[0];
			UStateTreeState* ParentState = Current.Get<0>();
			UStateTreeState* State = Current.Get<1>();
			check(State);

			Stack.RemoveAt(0);

			bContinue = InFunc(*State, ParentState) == EStateTreeVisitor::Continue;
			
			if (bContinue)
			{
				// Children
				for (UStateTreeState* ChildState : State->Children)
				{
					Stack.Add(FStatePair(State, ChildState));
				}
			}
		}
		
		if (!bContinue)
		{
			break;
		}
	}

	return EStateTreeVisitor::Continue;
}

void UStateTreeEditorData::VisitHierarchyNodes(TFunctionRef<EStateTreeVisitor(const UStateTreeState* State, const FGuid& ID, const FName& Name, const EStateTreeNodeType NodeType, const UScriptStruct* NodeStruct, const UStruct* InstanceStruct)> InFunc) const
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	VisitHierarchy([this, &InFunc](const UStateTreeState& State, UStateTreeState* /*ParentState*/)
	{
		return VisitStateNodes(State, InFunc);
	});
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

EStateTreeVisitor UStateTreeEditorData::VisitGlobalNodes(TFunctionRef<EStateTreeVisitor(const UStateTreeState* State, const FStateTreeBindableStructDesc& Desc, const FStateTreeDataView Value)> InFunc) const
{
	// Root parameters
	{
		FStateTreeBindableStructDesc Desc;
		Desc.StatePath = UE::StateTree::Editor::GlobalStateName;
		Desc.Struct = RootParameters.Parameters.GetPropertyBagStruct();
		Desc.Name = FName(TEXT("Parameters"));
		Desc.ID = RootParameters.ID;
		Desc.DataSource = EStateTreeBindableStructSource::Parameter;
		
		if (InFunc(nullptr, Desc, FStateTreeDataView(const_cast<FInstancedPropertyBag&>(RootParameters.Parameters).GetMutableValue())) == EStateTreeVisitor::Break)
		{
			return EStateTreeVisitor::Break;
		}
	}

	// All named external data items declared by the schema
	if (Schema != nullptr)
	{
		for (const FStateTreeExternalDataDesc& ContextDesc : Schema->GetContextDataDescs())
		{
			FStateTreeBindableStructDesc Desc;
			Desc.StatePath = UE::StateTree::Editor::GlobalStateName;
			Desc.Struct = ContextDesc.Struct;
			Desc.Name = ContextDesc.Name;
			Desc.ID = ContextDesc.ID;
			Desc.DataSource = EStateTreeBindableStructSource::Context;

			// We don't have value for the external objects, but return the type and null value so that users of GetAllStructValues() can use the type. 
			if (InFunc(nullptr, Desc, FStateTreeDataView(Desc.Struct, nullptr)) == EStateTreeVisitor::Break)
			{
				return EStateTreeVisitor::Break;
			}
		}
	}

	auto VisitFuncNodesFunc = [&InFunc, this](const FStateTreeEditorNode& Node)
	{
		const FString StatePath = FString::Printf(TEXT("%s/%s"), *UE::StateTree::Editor::GlobalStateName, *Node.GetName().ToString());
		return VisitStructBoundPropertyFunctions(Node.ID, StatePath, [&InFunc](const FStateTreeEditorNode& EditorNode, const FStateTreeBindableStructDesc& Desc, FStateTreeDataView Value)
		{
			return InFunc(nullptr, Desc, Value);
		});
	};

	// Evaluators
	for (const FStateTreeEditorNode& Node : Evaluators)
	{
		if (VisitFuncNodesFunc(Node) == EStateTreeVisitor::Break)
		{
			return EStateTreeVisitor::Break;
		}

		if (const FStateTreeEvaluatorBase* Evaluator = Node.Node.GetPtr<FStateTreeEvaluatorBase>())
		{
			FStateTreeBindableStructDesc Desc;
			Desc.StatePath = UE::StateTree::Editor::GlobalStateName;
			Desc.Struct = Evaluator->GetInstanceDataType();
			Desc.Name = Node.GetName();
			Desc.ID = Node.ID;
			Desc.DataSource = EStateTreeBindableStructSource::Evaluator;

			if (InFunc(nullptr, Desc, Node.GetInstance()) == EStateTreeVisitor::Break)
			{
				return EStateTreeVisitor::Break;
			}
		}
	}

	// Global tasks
	for (const FStateTreeEditorNode& Node : GlobalTasks)
	{
		if (VisitFuncNodesFunc(Node) == EStateTreeVisitor::Break)
		{
			return EStateTreeVisitor::Break;
		}

		if (const FStateTreeTaskBase* Task = Node.Node.GetPtr<FStateTreeTaskBase>())
		{
			FStateTreeBindableStructDesc Desc;
			Desc.StatePath = UE::StateTree::Editor::GlobalStateName;
			Desc.Struct = Task->GetInstanceDataType();
			Desc.Name = Node.GetName();
			Desc.ID = Node.ID;
			Desc.DataSource = EStateTreeBindableStructSource::GlobalTask;

			if (InFunc(nullptr, Desc, Node.GetInstance()) == EStateTreeVisitor::Break)
			{
				return EStateTreeVisitor::Break;
			}
		}
	}

	return  EStateTreeVisitor::Continue;
}

EStateTreeVisitor UStateTreeEditorData::VisitHierarchyNodes(TFunctionRef<EStateTreeVisitor(const UStateTreeState* State, const FStateTreeBindableStructDesc& Desc, const FStateTreeDataView Value)> InFunc) const
{
	return VisitHierarchy([this, &InFunc](const UStateTreeState& State, UStateTreeState* /*ParentState*/)
	{
		return VisitStateNodes(State, InFunc);
	});
}

EStateTreeVisitor UStateTreeEditorData::VisitAllNodes(TFunctionRef<EStateTreeVisitor(const UStateTreeState* State, const FStateTreeBindableStructDesc& Desc, const FStateTreeDataView Value)> InFunc) const
{
	if (VisitGlobalNodes(InFunc) == EStateTreeVisitor::Break)
	{
		return EStateTreeVisitor::Break;
	}

	if (VisitHierarchyNodes(InFunc) == EStateTreeVisitor::Break)
	{
		return EStateTreeVisitor::Break;
	}

	return EStateTreeVisitor::Continue;
}

#if WITH_STATETREE_TRACE_DEBUGGER
bool UStateTreeEditorData::HasAnyBreakpoint(const FGuid ID) const
{
	return Breakpoints.ContainsByPredicate([ID](const FStateTreeEditorBreakpoint& Breakpoint) { return Breakpoint.ID == ID; });
}

bool UStateTreeEditorData::HasBreakpoint(const FGuid ID, const EStateTreeBreakpointType BreakpointType) const
{
	return GetBreakpoint(ID, BreakpointType) != nullptr;
}

const FStateTreeEditorBreakpoint* UStateTreeEditorData::GetBreakpoint(const FGuid ID, const EStateTreeBreakpointType BreakpointType) const
{
	return Breakpoints.FindByPredicate([ID, BreakpointType](const FStateTreeEditorBreakpoint& Breakpoint)
		{
			return Breakpoint.ID == ID && Breakpoint.BreakpointType == BreakpointType;
		});
}

void UStateTreeEditorData::AddBreakpoint(const FGuid ID, const EStateTreeBreakpointType BreakpointType)
{
	Breakpoints.Emplace(ID, BreakpointType);

	const UStateTree* StateTree = GetTypedOuter<UStateTree>();
	checkf(StateTree, TEXT("UStateTreeEditorData should only be allocated within a UStateTree"));
	UE::StateTree::Delegates::OnBreakpointsChanged.Broadcast(*StateTree);
}

bool UStateTreeEditorData::RemoveBreakpoint(const FGuid ID, const EStateTreeBreakpointType BreakpointType)
{
	const int32 Index = Breakpoints.IndexOfByPredicate([ID, BreakpointType](const FStateTreeEditorBreakpoint& Breakpoint)
		{
			return Breakpoint.ID == ID && Breakpoint.BreakpointType == BreakpointType;
		});
		
	if (Index != INDEX_NONE)
	{
		Breakpoints.RemoveAtSwap(Index);
		
		const UStateTree* StateTree = GetTypedOuter<UStateTree>();
		checkf(StateTree, TEXT("UStateTreeEditorData should only be allocated within a UStateTree"));
		UE::StateTree::Delegates::OnBreakpointsChanged.Broadcast(*StateTree);
	}

	return Index != INDEX_NONE;
}

#endif // WITH_STATETREE_TRACE_DEBUGGER

PRAGMA_DISABLE_DEPRECATION_WARNINGS
void UStateTreeEditorData::AddPropertyBinding(const FStateTreeEditorPropertyPath& SourcePath, const FStateTreeEditorPropertyPath& TargetPath)
{
	EditorBindings.AddPropertyBinding(UE::StateTree::Private::ConvertEditorPath(SourcePath), UE::StateTree::Private::ConvertEditorPath(TargetPath));
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

#undef LOCTEXT_NAMESPACE