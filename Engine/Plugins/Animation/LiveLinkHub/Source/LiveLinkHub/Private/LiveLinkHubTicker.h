// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "LiveLinkHub.h"
#include "Misc/Timespan.h"
#include "Settings/LiveLinkHubSettings.h"

/** Object used to tick LiveLinkHub outside of the game thread. */
class FLiveLinkHubTicker : public FRunnable
{
public:
	void StartTick()
	{
		if (!bIsRunning)
		{
			bIsRunning = true;
			TickEvent = FGenericPlatformProcess::GetSynchEventFromPool();
			check(TickEvent);
			Thread.Reset(FRunnableThread::Create(this, TEXT("LiveLinkHubTicker")));
		}
	}

	//~ Begin FRunnable Interface
	virtual uint32 Run() override
	{
		float TickFrequency = 1 / GetDefault<ULiveLinkHubSettings>()->TargetFrameRate;
		FTimespan TickTimeSpan = FTimespan::FromSeconds(TickFrequency);

		while (bIsRunning)
		{
			check(TickEvent);
			TickEvent->Wait(TickTimeSpan);

			if (bIsRunning) // make sure we were not told to exit during the wait
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(FLiveLinkHubTicker::Tick);
				OnTickDelegate.Broadcast();
			}
		}

		return 0;
	}
	virtual void Exit() override
	{
		if (!bIsRunning)
		{
			return;
		}

		bIsRunning = false;

		if (TickEvent)
		{
			TickEvent->Trigger();
		}

		Thread->WaitForCompletion();

		if (TickEvent)
		{
			FGenericPlatformProcess::ReturnSynchEventToPool(TickEvent);
			TickEvent = nullptr;
		}
	}
	//~ End FRunnable Interface

public:

	/** Get the delegate called whenever this object ticks. */
	FTSSimpleMulticastDelegate& OnTick() { return OnTickDelegate;  }
private:

	std::atomic<bool> bIsRunning = false;

	/** Delegate called when this ticks. */
	FTSSimpleMulticastDelegate OnTickDelegate;

	FEvent* TickEvent = nullptr;
	TUniquePtr<FRunnableThread> Thread;
};
