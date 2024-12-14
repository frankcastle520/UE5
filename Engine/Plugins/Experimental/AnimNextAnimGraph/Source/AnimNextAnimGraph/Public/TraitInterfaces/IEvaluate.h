// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TraitCore/ExecutionContext.h"
#include "TraitCore/ITraitInterface.h"
#include "TraitCore/TraitBinding.h"
#include "EvaluationVM/EvaluationProgram.h"
#include "EvaluationVM/KeyframeState.h"

namespace UE::AnimNext
{
	/**
	 * FEvaluateTraversalContext
	 *
	 * Contains all relevant transient data for an evaluate traversal and wraps the execution context.
	 */
	struct FEvaluateTraversalContext final : FExecutionContext
	{
		// Appends a new task into the evaluation program, tasks mutate state in the order they have been appended in
		// This means that child nodes need to evaluate first, tasks will usually be appended in IEvaluate::PostEvaluate
		// Tasks are moved into their final memory location, caller can allocate the task anywhere, it is no longer needed after this operation
		// @see FEvaluationProgram, FEvaluationTask, FEvaluationVM
		template<class TaskType>
		void AppendTask(TaskType&& Task) { EvaluationProgram.AppendTask(MoveTemp(Task)); }

	private:
		explicit FEvaluateTraversalContext(FEvaluationProgram& InEvaluationProgram);

		FEvaluationProgram& EvaluationProgram;

		friend ANIMNEXTANIMGRAPH_API FEvaluationProgram EvaluateGraph(const FWeakTraitPtr& GraphRootPtr);
	};

	/**
	 * IEvaluate
	 * 
	 * This interface is called during the evaluation traversal. It aims to produce an evaluation program.
	 * 
	 * When a node is visited, PreEvaluate is first called on its top trait. It is responsible for forwarding
	 * the call to the next trait that implements this interface on the trait stack of the node. Once
	 * all traits have had the chance to PreEvaluate, the children of the trait are queried through
	 * the IHierarchy interface. The children will then evaluate and PostEvaluate will then be called afterwards
	 * on the original trait.
	 * 
	 * The execution context contains what to evaluate.
	 * @see FEvaluationProgram
	 */
	struct ANIMNEXTANIMGRAPH_API IEvaluate : ITraitInterface
	{
		DECLARE_ANIM_TRAIT_INTERFACE(IEvaluate, 0xa303e9e7)

		// Called before a traits children are evaluated
		virtual void PreEvaluate(FEvaluateTraversalContext& Context, const TTraitBinding<IEvaluate>& Binding) const;

		// Called after a traits children have been evaluated
		virtual void PostEvaluate(FEvaluateTraversalContext& Context, const TTraitBinding<IEvaluate>& Binding) const;

#if WITH_EDITOR
		virtual const FText& GetDisplayName() const override;
		virtual const FText& GetDisplayShortName() const override;
#endif // WITH_EDITOR
	};

	/**
	 * Specialization for trait binding.
	 */
	template<>
	struct TTraitBinding<IEvaluate> : FTraitBinding
	{
		// @see IEvaluate::PreEvaluate
		void PreEvaluate(FEvaluateTraversalContext& Context) const
		{
			GetInterface()->PreEvaluate(Context, *this);
		}

		// @see IEvaluate::PostEvaluate
		void PostEvaluate(FEvaluateTraversalContext& Context) const
		{
			GetInterface()->PostEvaluate(Context, *this);
		}

	protected:
		const IEvaluate* GetInterface() const { return GetInterfaceTyped<IEvaluate>(); }
	};

	/**
	 * Evaluates a sub-graph starting at its root and produces an evaluation program.
	 * Evaluation should be deterministic and repeated calls should yield the same evaluation program.
	 *
	 * For each node:
	 *     - We call PreEvaluate on all its traits
	 *     - We call GetChildren on all its traits
	 *     - We evaluate all children found
	 *     - We call PostEvaluate on all its traits
	 *
	 * @see IEvaluate::PreEvaluate, IEvaluate::PostEvaluate, IHierarchy::GetChildren
	 */
	[[nodiscard]] ANIMNEXTANIMGRAPH_API FEvaluationProgram EvaluateGraph(const FAnimNextGraphInstancePtr& GraphInstance);

	/**
	 * Evaluates a sub-graph starting at its root and produces an evaluation program.
	 * Evaluation starts at the top of the stack that includes the graph root trait.
	 * Evaluation should be deterministic and repeated calls should yield the same evaluation program.
	 *
	 * For each node:
	 *     - We call PreEvaluate on all its traits
	 *     - We call GetChildren on all its traits
	 *     - We evaluate all children found
	 *     - We call PostEvaluate on all its traits
	 *
	 * @see IEvaluate::PreEvaluate, IEvaluate::PostEvaluate, IHierarchy::GetChildren
	 */
	[[nodiscard]] ANIMNEXTANIMGRAPH_API FEvaluationProgram EvaluateGraph(const FWeakTraitPtr& GraphRootPtr);
}
