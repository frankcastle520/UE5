// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "QuartzSubscriptionToken.h"
#include "Sound/QuartzInterfaces.h"
#include "Sound/QuartzCommandQueue.h"
#include "Sound/QuartzQuantizationUtilities.h"
#include "Containers/ConsumeAllMpmcQueue.h"

// forwards
class UQuartzSubsystem;
class UQuartzClockHandle;
class FQuartzTickableObject;
struct FQuartzTickableObjectsManager;

namespace Audio
{
	struct FQuartzGameThreadSubscriber;
	class FQuartzClockProxy;

	// old version of TQuartzCommandQueue
	template<class ListenerType>
	class UE_DEPRECATED(5.5, "Message") TQuartzShareableCommandQueue
	{
	};
} // namespace Audio

/**
 *	FQuartzTickableObject
 *
 *		This is the base class for non-Audio Render Thread objects that want to receive
 *		callbacks for Quartz events.
 *
 *		It is a wrapper around TQuartzShareableCommandQueue.
 *		(see UQuartzClockHandle or UAudioComponent as implementation examples)
 */
using namespace Audio::Quartz;

// TODO: comment up why we listen to these interfaces
class ENGINE_API FQuartzTickableObject
	: public FQuartzSubscriberCommandQueue::TConsumerBase<
		  IMetronomeEventListener
		, ICommandListener
		, IQueueCommandListener
>
{
public:
	// ctor
	FQuartzTickableObject();

    FQuartzTickableObject(const FQuartzTickableObject& Other) = default;
    FQuartzTickableObject& operator=(const FQuartzTickableObject&) = default;

	// dtor
	virtual ~FQuartzTickableObject() override;

	FQuartzTickableObject* Init(UWorld* InWorldPtr);

	// called by the associated QuartzSubsystem
	void QuartzTick(float DeltaTime);

	bool QuartzIsTickable() const;

	void AddMetronomeBpDelegate(EQuartzCommandQuantization InQuantizationBoundary, const FOnQuartzMetronomeEventBP& OnQuantizationEvent);

	bool IsInitialized() const { return QuartzSubscriptionToken.IsSubscribed(); }

	Audio::FQuartzGameThreadSubscriber GetQuartzSubscriber();

	int32 AddCommandDelegate(const FOnQuartzCommandEventBP& InDelegate);

	UE_DEPRECATED(5.5, "This should not be called directly, use the ICommandListener interface instead.")
	void ExecCommand(const Audio::FQuartzQuantizedCommandDelegateData& Data) { OnCommandEvent(Data); }
	
	UE_DEPRECATED(5.5, "This should not be called directly, use the IMetronomeEventListener interface instead.")
	void ExecCommand(const Audio::FQuartzMetronomeDelegateData& Data) { OnMetronomeEvent(Data); }
	
	UE_DEPRECATED(5.5, "This should not be called directly, use the IQueueCommandListener interface instead.")
	void ExecCommand(const Audio::FQuartzQueueCommandData& Data) { OnQueueCommandEvent(Data);}

	// required by TQuartzShareableCommandQueue template
	virtual void OnCommandEvent(const Audio::FQuartzQuantizedCommandDelegateData& Data) override;
	virtual void OnMetronomeEvent(const Audio::FQuartzMetronomeDelegateData& Data) override;
	virtual void OnQueueCommandEvent(const Audio::FQuartzQueueCommandData& Data) override;

	// virtual interface (ExecCommand will forward the data to derived classes' ProcessCommand() call)
	virtual void ProcessCommand(const Audio::FQuartzQuantizedCommandDelegateData& Data) {}
	virtual void ProcessCommand(const Audio::FQuartzMetronomeDelegateData& Data) {}
	virtual void ProcessCommand(const Audio::FQuartzQueueCommandData& Data) {}

	const Audio::FQuartzOffset& GetQuartzOffset() const { return NotificationOffset; }

protected:
	void SetNotificationAnticipationAmountMilliseconds(const double Milliseconds);
	void SetNotificationAnticipationAmountMusicalDuration(const EQuartzCommandQuantization Duration,  const double Multiplier);

	void QuartzUnsubscribe();

private:
	struct FMetronomeDelegateGameThreadData
	{
		FOnQuartzMetronomeEvent MulticastDelegate;
	};

	struct FCommandDelegateGameThreadData
	{
		FOnQuartzCommandEvent MulticastDelegate;
		FThreadSafeCounter RefCount;
	};

	// delegate containers
	FMetronomeDelegateGameThreadData MetronomeDelegates[static_cast<int32>(EQuartzCommandQuantization::Count)];
	TArray<FCommandDelegateGameThreadData> QuantizedCommandDelegates;

	TArray<TFunction<void(FQuartzTickableObject*)>> TempCommandQueue;

private:
	Audio::FQuartzOffset NotificationOffset;
	FQuartzGameThreadCommandQueuePtr CommandQueuePtr;
	FQuartzSubscriptionToken QuartzSubscriptionToken;
}; // class FQuartzTickableObject

