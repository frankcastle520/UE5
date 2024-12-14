// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlayAnim/PlayAnimRequest.h"

#include "PlayAnim/PlayAnimEvents.h"
#include "Component/AnimNextComponent.h"

namespace UE::AnimNext
{
	bool FPlayAnimRequest::Play(FPlayAnimRequestArgs&& InRequestArgs, UAnimNextComponent* InComponent)
	{
		check(IsInGameThread());
		if (!InRequestArgs.Payload.IsValid() || InComponent == nullptr)
		{
			return false;	// Nothing to play
		}

		if (Status != EPlayAnimStatus::None)
		{
			return false;	// Already playing, cannot play again
		}

		RequestArgs = MoveTemp(InRequestArgs);
		Component = InComponent;
		Status = EPlayAnimStatus::Pending;

		auto StartEvent = MakeTraitEvent<FPlayAnim_PlayEvent>();
		StartEvent->Request = AsShared();

		Component->QueueInputTraitEvent(StartEvent);

		PendingStartEvent = StartEvent;

		return true;
	}

	void FPlayAnimRequest::Stop()
	{
		check(IsInGameThread());
		if (!EnumHasAnyFlags(Status, EPlayAnimStatus::Playing))
		{
			return;	// Not playing
		}
		else if (EnumHasAnyFlags(Status, EPlayAnimStatus::Interrupted))
		{
			return;	// We already got interrupted
		}

		// If we are still pending, just cancel our event
		if (PendingStartEvent)
		{
			PendingStartEvent->MarkConsumed();
			PendingStartEvent = nullptr;
		}
		else
		{
			auto StopEvent = MakeTraitEvent<FPlayAnim_StopEvent>();
			StopEvent->Request = AsShared();

			Component->QueueInputTraitEvent(StopEvent);
		}
	}

	const FPlayAnimRequestArgs& FPlayAnimRequest::GetArgs() const
	{
		return RequestArgs;
	}

	FPlayAnimRequestArgs& FPlayAnimRequest::GetMutableArgs()
	{
		return RequestArgs;
	}

	EPlayAnimStatus FPlayAnimRequest::GetStatus() const
	{
		check(IsInGameThread());
		return Status;
	}

	FTimelineProgress FPlayAnimRequest::GetTimelineProgress() const
	{
		check(IsInGameThread());
		return TimelineProgress;
	}

	bool FPlayAnimRequest::HasExpired() const
	{
		check(IsInGameThread());
		return EnumHasAnyFlags(Status, EPlayAnimStatus::Expired);
	}

	bool FPlayAnimRequest::HasCompleted() const
	{
		check(IsInGameThread());
		return EnumHasAnyFlags(Status, EPlayAnimStatus::Completed);
	}

	bool FPlayAnimRequest::IsPlaying() const
	{
		check(IsInGameThread());
		return EnumHasAnyFlags(Status, EPlayAnimStatus::Playing);
	}

	bool FPlayAnimRequest::IsBlendingOut() const
	{
		check(IsInGameThread());
		return EnumHasAnyFlags(Status, EPlayAnimStatus::BlendingOut);
	}

	bool FPlayAnimRequest::WasInterrupted() const
	{
		check(IsInGameThread());
		return EnumHasAnyFlags(Status, EPlayAnimStatus::Interrupted);
	}

	void FPlayAnimRequest::OnStatusUpdate(EPlayAnimStatus NewStatus)
	{
		check(IsInGameThread());
		switch (NewStatus)
		{
		case EPlayAnimStatus::Playing:
			ensureMsgf(Status == EPlayAnimStatus::Pending, TEXT("Expected PlayAnim status to be pending, found: %u"), (uint32)Status);
			Status = NewStatus;
			PendingStartEvent = nullptr;
			OnStarted.ExecuteIfBound(*this);
			break;
		case EPlayAnimStatus::Playing | EPlayAnimStatus::Interrupted:
			ensureMsgf(EnumHasAnyFlags(Status, EPlayAnimStatus::Playing), TEXT("Expected PlayAnim status to be playing, found: %u"), (uint32)Status);
			EnumAddFlags(Status, EPlayAnimStatus::Interrupted);
			OnInterrupted.ExecuteIfBound(*this);
			break;
		case EPlayAnimStatus::BlendingOut:
			ensureMsgf(EnumHasAnyFlags(Status, EPlayAnimStatus::Playing), TEXT("Expected PlayAnim status to be playing, found: %u"), (uint32)Status);
			EnumAddFlags(Status, EPlayAnimStatus::BlendingOut);
			OnBlendingOut.ExecuteIfBound(*this);
			break;
		case EPlayAnimStatus::Completed:
			ensureMsgf(EnumHasAnyFlags(Status, EPlayAnimStatus::Playing), TEXT("Expected PlayAnim status to be playing, found: %u"), (uint32)Status);
			// Maintain our interrupted status if it was present
			Status = EPlayAnimStatus::Completed | (Status & EPlayAnimStatus::Interrupted);
			OnCompleted.ExecuteIfBound(*this);
			break;
		case EPlayAnimStatus::Expired:
			ensureMsgf(Status == EPlayAnimStatus::Pending, TEXT("Expected PlayAnim status to be pending, found: %u"), (uint32)Status);
			Status = NewStatus;
			OnCompleted.ExecuteIfBound(*this);
			break;
		default:
			ensureMsgf(false, TEXT("Unsupported PlayAnim status update value: %u"), (uint32)NewStatus);
			break;
		}
	}

	void FPlayAnimRequest::OnTimelineUpdate(FTimelineProgress NewTimelineProgress)
	{
		check(IsInGameThread());
		TimelineProgress = NewTimelineProgress;
	}

	void FPlayAnimRequest::AddReferencedObjects(FReferenceCollector& Collector)
	{
		Collector.AddReferencedObject(RequestArgs.Object);
		Collector.AddPropertyReferencesWithStructARO(FInstancedPropertyBag::StaticStruct(), &RequestArgs.Payload);
		Collector.AddReferencedObject(Component);
	}
}
