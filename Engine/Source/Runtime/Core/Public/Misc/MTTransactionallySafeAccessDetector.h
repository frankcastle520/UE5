// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AutoRTFM/AutoRTFM.h"
#include "HAL/Platform.h"
#include "MTAccessDetector.h"
#include "Misc/AssertionMacros.h"

#if ENABLE_MT_DETECTOR

/**
 * A version of FRWAccessDetector which also can be used in AutoRTFM transactions.
 *
 * When methods are called outside of a closed transaction they will behave
 * exactly the same as FRWAccessDetector.
 *
 * When called from a closed transaction:
 *  - The first call to AcquireWriteLock() will hold the internal write lock for
 *    the remaining duration of the transaction, even if the transaction calls
 *    ReleaseWriteLock(). This is done to guard against writes that would occur
 *    if the transaction were aborted. The transaction's use of the API remains
 *    unchanged, but unlocks and read locks are effectively no-ops and other 
 *    threads will "see" a longer duration of the write lock.
 *  - Each call to [Acquire|Release][Read|Write]Access is recorded as a pair
 *    of signed read and write counters so that the OnAbort and OnCommit
 *    handlers can restore or apply the read and write lock state, respectively.
 */
struct FRWTransactionallySafeAccessDetectorDefinition
{
public:
	/**
	 * Acquires read access, will check if there are any writers
	 * @return true if no errors were detected
	 */
	FORCEINLINE bool AcquireReadAccess() const
	{
		if (!AutoRTFM::IsClosed())
		{
			// Called outside of a closed transaction.
			// Forward to the inner detector and return.
			return Inner.AcquireReadAccess();
		}

		// AcquireReadAccess() called inside a closed transaction.
		return AutoRTFM::Open([this]
		{
			if (!TransactionalState.HoldsInternalWriteLock)
			{
				// AcquireReadAccess() called before the first transactional
				// call to AcquireWriteAccess(). Call the inner detector.
				if (UNLIKELY(!Inner.AcquireReadAccess()))
				{
					return false;
				}

				// Register the abort handler if this is the first transactional
				// method call.
				MaybeRegisterAbortHandler();
			}

			// Increment the transactional read-lock counter.
			TransactionalState.ReadLockDelta++;
			return true;
		});
	}

	/**
	 * Releases read access, will check if there are any writers
	 * @return true if no errors were detected
	 */
	FORCEINLINE bool ReleaseReadAccess() const
	{
		if (!AutoRTFM::IsClosed())
		{
			// Called outside of a closed transaction.
			// Forward to the inner detector and return.
			return Inner.ReleaseReadAccess();
		}

		// ReleaseReadAccess() called inside a closed transaction.
		return AutoRTFM::Open([this]
		{
			if (!TransactionalState.HoldsInternalWriteLock)
			{
				// ReleaseReadAccess() called before the first transactional
				// call to AcquireWriteAccess(). Call the inner detector.
				if (UNLIKELY(!Inner.ReleaseReadAccess()))
				{
					return false;
				}

				// Register the abort handler if this is the first transactional
				// method call.
				MaybeRegisterAbortHandler();
			}

			// Decrement the transactional read-lock counter.
			TransactionalState.ReadLockDelta--;
			return true;
		});
	}

	/** 
	 * Acquires write access, will check if there are readers or other writers
	 * @return true if no errors were detected
	 */
	FORCEINLINE bool AcquireWriteAccess() const
	{
		if (!AutoRTFM::IsClosed())
		{
			// Called outside of a closed transaction.
			// Forward to the inner detector and return.
			return Inner.AcquireWriteAccess();
		}

		return AutoRTFM::Open([this]
		{
			if (!TransactionalState.HoldsInternalWriteLock)
			{
				// First transactional call to AcquireWriteAccess(). 
				// Call the inner detector.
				if (UNLIKELY(!Inner.AcquireWriteAccess()))
				{
					return false;
				}

				// Register the abort handler if this is the first transactional
				// method call.
				MaybeRegisterAbortHandler();
				// Register the commit handler to unlock the write-lock which
				// will be held until the transaction is done.
				RegisterCommitHandler();
				TransactionalState.HoldsInternalWriteLock = true;
			}

			// Increment the transactional write-lock counter.
			TransactionalState.WriteLockDelta++;
			return true;
		});
	}

	/** 
	 * Releases write access, will check if there are readers or other writers
	 * @return true if no errors were detected
	 */
	FORCEINLINE bool ReleaseWriteAccess() const
	{
		if (!AutoRTFM::IsClosed())
		{
			// Called outside of a closed transaction.
			// Forward to the inner detector and return.
			return Inner.ReleaseWriteAccess();
		}

		return AutoRTFM::Open([this]
		{
			if (!TransactionalState.HoldsInternalWriteLock)
			{
				// ReleaseWriteAccess() called before the first transactional
				// call to AcquireWriteAccess(). Call the inner detector.
				if (UNLIKELY(!Inner.ReleaseWriteAccess()))
				{
					return false;
				}

				// Register the abort handler if this is the first transactional
				// method call.
				MaybeRegisterAbortHandler();
			}

			// Decrement the transactional write-lock counter.
			TransactionalState.WriteLockDelta--;
			return true;
		});
	}

private:
	// Registers a transaction abort handler if this is the first call for the
	// current transaction.
	void MaybeRegisterAbortHandler() const
	{
		if (TransactionalState.AbortHandlerRegistered)
		{
			return; // Already registered.
		}
		TransactionalState.AbortHandlerRegistered = true;

		// Call OnAbort() in the closed, otherwise it'll be a no-op.
		(void) AutoRTFM::Close([&]
		{
			AutoRTFM::OnAbort([this]
			{
				// Transaction is being aborted. Undo all state changes.
				if (TransactionalState.HoldsInternalWriteLock)
				{
					// AcquireWriteAccess() was called in the transaction.
					// The initial state when entering the transaction must have
					// been write-unlocked, otherwise we'd double-write-lock.
					// Unlock the write.
					Inner.ReleaseWriteAccess();
				}
				else
				{
					// AcquireWriteAccess() was not called in the transaction.
					// Undo any transactional call ReleaseWriteAccess().
					if (TransactionalState.WriteLockDelta < 0)
					{
						Inner.AcquireWriteAccess();
					}
					// Undo any transactional calls AcquireReadAccess().
					for (; TransactionalState.ReadLockDelta > 0; TransactionalState.ReadLockDelta--)
					{
						Inner.ReleaseReadAccess();
					}
				}
				
				// Undo any transactional calls ReleaseReadAccess().
				for (; TransactionalState.ReadLockDelta < 0; TransactionalState.ReadLockDelta++)
				{
					Inner.AcquireReadAccess();
				}

				// Clear the TransactionalState for the next transaction.
				TransactionalState = {};
			});
		});
	}

	// Registers a transaction commit handler to rebalance locks after the first
	// call to AcquireWriteAccess().
	void RegisterCommitHandler() const
	{
		// Call OnCommit() in the closed, otherwise it'll invoke the callback 
		// immediately instead of when the transaction is committed. 
		(void) AutoRTFM::Close([&]
		{
			AutoRTFM::OnCommit([this]
			{
				// Transaction is being committed.
				check(TransactionalState.HoldsInternalWriteLock);
				check(TransactionalState.WriteLockDelta >= 0);
				// Release the write-lock, if the write-lock is now balanced.
				if (TransactionalState.WriteLockDelta == 0)
				{
					Inner.ReleaseWriteAccess();
				}
				// Calls to AcquireReadAccess() with the write-lock held 
				// were a no-op, so apply them now.
				for (; TransactionalState.ReadLockDelta > 0; TransactionalState.ReadLockDelta--)
				{
					Inner.AcquireReadAccess();
				}

				// Clear the TransactionalState for the next transaction.
				TransactionalState = {};
			});
		});
	}

	struct FTransactionalState
	{
		// Incremented for each transactional call to AcquireReadAccess(),
		// decremented for each transactional call to ReleaseReadAccess().
		int16 ReadLockDelta = 0;

		// Incremented for each transactional call to AcquireWriteAccess(),
		// decremented for each transactional call to ReleaseWriteAccess().
		int16 WriteLockDelta = 0;
		
		// If true, then the write-lock has been taken in a transaction and will
		// be held until the transaction is complete (either aborted or 
		// committed).
		bool HoldsInternalWriteLock = false;

		// True if the abort handler has been registered.
		bool AbortHandlerRegistered = false;
	};

	// The inner FRWAccessDetector.
	FRWAccessDetector Inner;

	// The state held for calls made when in a transaction.
	mutable FTransactionalState TransactionalState;
};

#if UE_AUTORTFM
using FRWTransactionallySafeAccessDetector = FRWTransactionallySafeAccessDetectorDefinition;
#else
using FRWTransactionallySafeAccessDetector = FRWAccessDetector;
#endif

#define UE_MT_DECLARE_TS_RW_ACCESS_DETECTOR(AccessDetector) FRWTransactionallySafeAccessDetector AccessDetector;

#else // ENABLE_MT_DETECTOR

#define UE_MT_DECLARE_TS_RW_ACCESS_DETECTOR(AccessDetector)

#endif // ENABLE_MT_DETECTOR
