// Copyright Epic Games, Inc. All Rights Reserved.

#include "GenericPlatform/GenericPlatformManualResetEvent.h"

#include "Async/Fundamental/Scheduler.h"

namespace UE::HAL::Private
{
	bool FGenericPlatformManualResetEvent::WaitUntil(FMonotonicTimePoint WaitTime)
	{
		std::unique_lock SelfLock(Lock);
		if (WaitTime.IsInfinity())
		{
			LowLevelTasks::FOversubscriptionScope _;
			Condition.wait(SelfLock, [this] { return !bWait.load(std::memory_order_acquire); });
			return true;
		}
		if (FMonotonicTimeSpan WaitSpan = WaitTime - FMonotonicTimePoint::Now(); WaitSpan > FMonotonicTimeSpan::Zero())
		{
			LowLevelTasks::FOversubscriptionScope _;
			const int64 WaitMs = FPlatformMath::CeilToInt64(WaitSpan.ToMilliseconds());
			return Condition.wait_for(SelfLock, std::chrono::milliseconds(WaitMs), [this] { return !bWait.load(std::memory_order_acquire); });
		}
		return !bWait.load(std::memory_order_acquire);
	}

} // UE::HAL::Private
