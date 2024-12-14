// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VerseVM/VVMCell.h"
#include "VerseVM/VVMGlobalTrivialEmergentTypePtr.h"
#include "VerseVM/VVMRestValue.h"
#include "VerseVM/VVMTransaction.h"
#include "VerseVM/VVMTree.h"
#include "VerseVM/VVMType.h"
#include "VerseVM/VVMWriteBarrier.h"

namespace Verse
{
struct FOp;
struct VFrame;
struct VTask;

struct VFailureContext : VCell
	, TIntrusiveTree<VFailureContext>
{
	DECLARE_DERIVED_VCPPCLASSINFO(COREUOBJECT_API, VCell);
	COREUOBJECT_API static TGlobalTrivialEmergentTypePtr<&StaticCppClassInfo> GlobalTrivialEmergentType;

	// TODO: We could organize this class to point to a "Rare Data" cell
	// that has fields that are populated just when leniency is encountered.

	// Base TIntrusiveTree is used to track children FailureContexts. The Next/Prev pointers
	// will only be populated if we encounter leniency, since they represent sibling
	// failure contexts. We only remove children from the parent tree after they're
	// done executing. A failure context is done executing when we run the EndFailureContext
	// opcode with no suspensions left, or when all suspensions created inside of it are
	// finished executing, or if failure is encountered.

	// Used to restore state when failure is encountered in *this* failure context.
	TWriteBarrier<VTask> Task;
	TWriteBarrier<VFrame> Frame;
	TWriteBarrier<VValue> IncomingEffectToken; // Used to restore the effect token when failure is encountered. Used both during lenient and non-lenient execution.
	FOp* FailurePC;

	// These fields are used during lenient execution.
	VRestValue BeforeThenEffectToken{0};
	VRestValue DoneEffectToken{0};
	FOp* ThenPC{nullptr};
	FOp* DonePC{nullptr};

	uint32 SuspensionCount{0};
	bool bFailed{false};
	bool bExecutedEndFailureContextOpcode{false};
	FTransaction Transaction;

	static VFailureContext& New(FAllocationContext Context, VTask* Task, VFailureContext* Parent, VFrame& Frame, VValue IncomingEffectToken, FOp* FailurePC)
	{
		return *new (Context.AllocateFastCell(sizeof(VFailureContext))) VFailureContext(Context, Task, Parent, Frame, IncomingEffectToken, FailurePC);
	}

	void FinishedExecuting(FAccessContext Context)
	{
		Detach(Context);
	}

	void Fail(FRunningContext Context)
	{
		// You can't have two sibling transactions that have started. The successor child
		// transaction can only begin after the predecessor child finishes.
		// For example, in this Verse code:
		// if (<ParentTransaction>):
		//     if (<Child1>): ...
		//     if (<Child2>): ...
		// Child2 can only start after Child1 commits or aborts. Therefore, the list of active
		// transactions forms a linked list, not a tree (even though we need to track the
		// tree of failure contexts).
		//
		// And because of how transactions work, we need to Abort beginning at the innermost
		// transaction and moving upwards towards the parent. Since we're just talking about
		// a linked list here, we count the number of started transactions and just abort the
		// Context's CurrentTransaction that many times.
		//
		// (If for some reason we found that we needed to call Abort on the entire failure context
		// tree, and needed to maintain that children needed to be aborted before parents, we could
		// just do a post order traversal to achieve that.)
		uint32 NumStartedTransactions = 0;

		ForEach([&](VFailureContext& FailureContext) {
			if (FailureContext.Transaction.bHasStarted)
			{
				++NumStartedTransactions;
			}
			else
			{
				FailureContext.Transaction.Abort(Context);
			}
			FailureContext.bFailed = true;
		});

		for (uint32 I = 0; I < NumStartedTransactions; ++I)
		{
			Context.CurrentTransaction()->Abort(Context);
		}
	}

private:
	VFailureContext(FAllocationContext Context, VTask* Task, VFailureContext* Parent, VFrame& Frame, VValue IncomingEffectToken, FOp* FailurePC)
		: VCell(Context, &GlobalTrivialEmergentType.Get(Context))
		, TIntrusiveTree(Context, Parent)
		, Task(Context, Task)
		, Frame(Context, Frame)
		, IncomingEffectToken(Context, IncomingEffectToken)
		, FailurePC(FailurePC)
	{
	}
};

} // namespace Verse

#endif
