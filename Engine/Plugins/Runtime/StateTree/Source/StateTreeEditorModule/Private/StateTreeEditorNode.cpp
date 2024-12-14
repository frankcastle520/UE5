// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeEditorNode.h"

#include "Blueprint/StateTreeConditionBlueprintBase.h"
#include "Blueprint/StateTreeEvaluatorBlueprintBase.h"
#include "Blueprint/StateTreeTaskBlueprintBase.h"

FName FStateTreeEditorNode::GetName() const
{
	const UScriptStruct* NodeType = Node.GetScriptStruct();
	if (!NodeType)
	{
		return FName();
	}

	if (const FStateTreeNodeBase* NodePtr = Node.GetPtr<FStateTreeNodeBase>())
	{
		if (NodePtr->Name.IsNone())
		{
			if (InstanceObject &&
					(NodeType->IsChildOf(TBaseStructure<FStateTreeBlueprintTaskWrapper>::Get())
					|| NodeType->IsChildOf(TBaseStructure<FStateTreeBlueprintEvaluatorWrapper>::Get())
					|| NodeType->IsChildOf(TBaseStructure<FStateTreeBlueprintConditionWrapper>::Get())))
			{
				return FName(InstanceObject->GetClass()->GetDisplayNameText().ToString());
			}
			return FName(NodeType->GetDisplayNameText().ToString());
		}
		return NodePtr->Name;
	}
	
	return FName();
}
