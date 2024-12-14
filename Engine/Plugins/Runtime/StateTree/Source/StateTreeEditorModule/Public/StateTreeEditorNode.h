// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StateTreeNodeBase.h"
#include "StateTreeEditorNode.generated.h"

UENUM()
enum class EStateTreeNodeType : uint8
{
	EnterCondition,
	Evaluator,
	Task,
	TransitionCondition,
	StateParameters,
	PropertyFunction,
};

/**
 * Base for Evaluator, Task and Condition nodes.
 */
USTRUCT()
struct STATETREEEDITORMODULE_API FStateTreeEditorNode
{
	GENERATED_BODY()

	void Reset()
	{
		Node.Reset();
		Instance.Reset();
		InstanceObject = nullptr;
		ID = FGuid();
	}

	/**
	 * This is used to name nodes for runtime, as well as for error reporting.
	 * If the node has a specified name, used that, or else of return the display name of the node.
	 * @return name of the node.
	 */
	FName GetName() const;

	FStateTreeDataView GetInstance() const
	{
		return InstanceObject ? FStateTreeDataView(InstanceObject) : FStateTreeDataView(const_cast<FInstancedStruct&>(Instance));
	}

	FStateTreeDataView GetInstance()
	{
		return InstanceObject ? FStateTreeDataView(InstanceObject) : FStateTreeDataView(Instance);
	}

	UPROPERTY(EditDefaultsOnly, Category = Node)
	FInstancedStruct Node;

	UPROPERTY(EditDefaultsOnly, Category = Node)
	FInstancedStruct Instance;

	UPROPERTY(EditDefaultsOnly, Instanced, Category = Node)
	TObjectPtr<UObject> InstanceObject = nullptr;
	
	UPROPERTY(EditDefaultsOnly, Category = Node)
	FGuid ID;

	UPROPERTY(EditDefaultsOnly, Category = Node)
	uint8 ExpressionIndent = 0;

	UPROPERTY(EditDefaultsOnly, Category = Node)
	EStateTreeExpressionOperand ExpressionOperand = EStateTreeExpressionOperand::And;

#if WITH_EDITOR
	UE_DEPRECATED(5.5, "Use ExpressionIndent instead.")
	uint8 ConditionIndent = 0;

PRAGMA_DISABLE_DEPRECATION_WARNINGS
	UE_DEPRECATED(5.5, "Use ExpressionOperand instead.")
	EStateTreeConditionOperand ConditionOperand = EStateTreeConditionOperand::And;

	FStateTreeEditorNode() = default;
	FStateTreeEditorNode(const FStateTreeEditorNode&) = default;
	FStateTreeEditorNode(FStateTreeEditorNode&&) = default;
	FStateTreeEditorNode& operator=(const FStateTreeEditorNode&) = default;
	FStateTreeEditorNode& operator=(FStateTreeEditorNode&&) = default;
	~FStateTreeEditorNode() = default;
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#endif //WITH_EDITOR
};

template <typename T>
struct TStateTreeEditorNode : public FStateTreeEditorNode
{
	using NodeType = T;
	FORCEINLINE T& GetNode() { return Node.template GetMutable<T>(); }
	FORCEINLINE typename T::FInstanceDataType& GetInstanceData() { return Instance.template GetMutable<typename T::FInstanceDataType>(); }
};
