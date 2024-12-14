// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/CriticalSection.h"
#include "AutoRTFM/AutoRTFM.h"
#include "Templates/SharedPointer.h"

#if UE_AUTORTFM

// A transactionally safe lock that works in the following novel ways:
	// - In the open (non-transactional):
	//   - Take the lock like before. Simple!
	//   - Free the lock like before too.
	// - In the closed (transactional):
	//   - During locking we query `TransactionalLockCount`:
	//	   - 0 means we haven't taken the lock within our transaction nest and need to acquire the lock.
	//     - Otherwise we already have the lock (and are preventing non-transactional code seeing any
	//       modifications we've made while holding the lock), so just bump `TransactionalLockCount`.
	//     - We also register an on-abort handler to release the lock should we abort (but we need to
	//       query `TransactionalLockCount` even there because we could be aborting an inner transaction
	//       and the parent transaction still wants to have the lock held!).
	//   - During unlocking we defer doing the unlock until the transaction commits.
	//
	// Thus with this approach we will hold this lock for the *entirety* of the transactional nest should
	// we take the lock during the transaction, thus preventing non-transactional code from seeing any
	// modifications we should make.
	//
	// If we are within a transaction, we pessimise our read-lock to a write-lock. Note: that it should
	// potentially be possible to have read-locks work correctly, but serious care will have to be taken to
	// ensure that we don't have:
	//   Open Thread     Closed Thread
	//   -----------     ReadLock
	//   -----------     ReadUnlock
	//   WriteLock       -------------
	//   WriteUnlock     -------------
	//   -----------     ReadLock      <- Invalid because the transaction can potentially observe side
	//                                    effects of the open-threads writes!
struct FTransactionallySafeRWLockDefinition final
{
	// Always open because the constructor arguments will create the underlying critical section.
	UE_AUTORTFM_ALWAYS_OPEN
	FTransactionallySafeRWLockDefinition() : State(MakeShared<FState>())
	{
		if (AutoRTFM::IsTransactional())
		{
			const AutoRTFM::EContextStatus Status = AutoRTFM::Close([this]
				{
					AutoRTFM::PushOnAbortHandler(this, [this]
						{
							this->~FTransactionallySafeRWLockDefinition();
						});
				});

			ensure(AutoRTFM::EContextStatus::OnTrack == Status);
		}
	}

	~FTransactionallySafeRWLockDefinition()
	{
		if (AutoRTFM::IsTransactional())
		{
			const AutoRTFM::EContextStatus Status = AutoRTFM::Close([this]
				{
					AutoRTFM::PopOnAbortHandler(this);

					// We explicitly copy the state here for the case that `this` was stack
					// allocated and has already died before the on-commit is hit.
					AutoRTFM::OnCommit([State = this->State]
						{
							ensure(0 == State->TransactionalLockCount);
						});
				});

			ensure(AutoRTFM::EContextStatus::OnTrack == Status);
		}

		// As the State was constructed in the open, it must be released in the open.
		AutoRTFM::Open([&] { State = nullptr; });
	}

	void ReadLock()
	{
		if (AutoRTFM::IsTransactional() || AutoRTFM::IsCommittingOrAborting())
		{
			// Transactionally pessimise ReadLock -> WriteLock.
			WriteLock();
		}
		else
		{
			State->Lock.ReadLock();
			ensure(0 == State->TransactionalLockCount);
		}
	}

	void ReadUnlock()
	{
		if (AutoRTFM::IsTransactional() || AutoRTFM::IsCommittingOrAborting())
		{
			// Transactionally pessimise ReadUnlock -> WriteUnlock.
			WriteUnlock();
		}
		else
		{
			ensure(0 == State->TransactionalLockCount);
			State->Lock.ReadUnlock();
		}
	}

	void WriteLock()
	{
		if (AutoRTFM::IsTransactional() || AutoRTFM::IsCommittingOrAborting())
		{
			AutoRTFM::Open([&]
				{
					// The transactional system which can increment TransactionalLockCount
					// is always single-threaded, thus this is safe to check without atomicity.
					if (0 == State->TransactionalLockCount)
					{
						State->Lock.WriteLock();
					}

					State->TransactionalLockCount += 1;
				});

			// We explicitly copy the state here for the case that `this` was stack
			// allocated and has already died before the on-abort is hit.
			AutoRTFM::OnAbort([State = this->State]
				{
					ensure(0 != State->TransactionalLockCount);

					State->TransactionalLockCount -= 1;

					if (0 == State->TransactionalLockCount)
					{
						State->Lock.WriteUnlock();
					}
				});
		}
		else
		{
			State->Lock.WriteLock();
			ensure(0 == State->TransactionalLockCount);
		}
	}

	void WriteUnlock()
	{
		if (AutoRTFM::IsTransactional() || AutoRTFM::IsCommittingOrAborting())
		{
			// We explicitly copy the state here for the case that `this` was stack
			// allocated and has already died before the on-commit is hit.
			AutoRTFM::OnCommit([State = this->State]
				{
					ensure(0 != State->TransactionalLockCount);

					State->TransactionalLockCount -= 1;

					if (0 == State->TransactionalLockCount)
					{
						State->Lock.WriteUnlock();
					}
				});
		}
		else
		{
			ensure(0 == State->TransactionalLockCount);
			State->Lock.WriteUnlock();
		}
	}

private:
	UE_NONCOPYABLE(FTransactionallySafeRWLockDefinition)

	struct FState final
	{
		FRWLock Lock;
		uint32 TransactionalLockCount = 0;
	};

	TSharedPtr<FState> State;
};

using FTransactionallySafeRWLock = FTransactionallySafeRWLockDefinition;

#else
using FTransactionallySafeRWLock = FRWLock;
#endif
