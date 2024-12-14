// Copyright Epic Games, Inc. All Rights Reserved.

#include "AvaTransitionTreeEditorData.h"

UAvaTransitionTreeEditorData::UAvaTransitionTreeEditorData()
{
	FStateTreeEditorColor DefaultColor;
	DefaultColor.ColorRef = FStateTreeEditorColorRef(UE::AvaTransitionEditor::ColorId_Default);
	DefaultColor.Color = FLinearColor(0, 0.15, 0.2);
	DefaultColor.DisplayName = TEXT("Default Color");

	FStateTreeEditorColor InColor;
	InColor.ColorRef = FStateTreeEditorColorRef(UE::AvaTransitionEditor::ColorId_In);
	InColor.Color = FLinearColor(0, 0.2, 0);
	InColor.DisplayName = TEXT("In Color");

	FStateTreeEditorColor OutColor;
	OutColor.ColorRef = FStateTreeEditorColorRef(UE::AvaTransitionEditor::ColorId_Out);
	OutColor.Color = FLinearColor(0.2, 0, 0.15);
	OutColor.DisplayName = TEXT("Out Color");

	Colors.Empty(3);
	Colors.Add(MoveTemp(DefaultColor));
	Colors.Add(MoveTemp(InColor));
	Colors.Add(MoveTemp(OutColor));
}

UStateTreeState& UAvaTransitionTreeEditorData::CreateState(const UStateTreeState& InSiblingState, bool bInAfter)
{
	UObject* Outer = this;
	if (InSiblingState.Parent)
	{
		Outer = InSiblingState.Parent;
	}

	check(Outer);
	UStateTreeState* State = NewObject<UStateTreeState>(Outer, NAME_None, RF_Transactional);
	check(State);

	State->Parent = InSiblingState.Parent;

	TArray<TObjectPtr<UStateTreeState>>& Children = State->Parent
		? State->Parent->Children
		: SubTrees;

	int32 ChildIndex = Children.IndexOfByKey(&InSiblingState);
	ChildIndex = FMath::Clamp(ChildIndex, 0, Children.Num() - 1);

	if (bInAfter)
	{
		++ChildIndex;
	}

	Children.Insert(State, ChildIndex);
	return *State;
}
