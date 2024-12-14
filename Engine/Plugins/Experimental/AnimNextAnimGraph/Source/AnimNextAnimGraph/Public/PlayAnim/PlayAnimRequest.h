// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "AlphaBlend.h"
#include "TraitCore/TraitEvent.h"
#include "TraitInterfaces/ITimeline.h"
#include "PlayAnim/PlayAnimStatus.h"
#include "StructUtils/PropertyBag.h"

#include "PlayAnimRequest.generated.h"

class UAnimNextDataInterface;
class FReferenceCollector;
class UAnimNextModule;
class UAnimNextComponent;
class UAnimSequence;
class UBlendProfile;

namespace UE::AnimNext
{
	struct FPlayAnim_PlayEvent;
	struct FPlayAnim_StatusUpdateEvent;
	struct FPlayAnim_TimelineUpdateEvent;
}

UENUM()
enum class EAnimNextPlayAnimBlendMode : uint8
{
	// Uses standard weight based blend
	Standard,

	// Uses inertialization. Requires an inertialization trait somewhere earlier in the graph.
	Inertialization,
};

/**
 * PlayAnim Blend Settings
 *
 * Encapsulates the blend settings used by Play animation requests.
 */
USTRUCT(BlueprintType)
struct FAnimNextPlayAnimBlendSettings
{
	GENERATED_BODY()

	/** Blend Profile to use for this blend */
	//UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blend", meta = (DisplayAfter = "Blend"))
	//TObjectPtr<UBlendProfile> BlendProfile;

	/** AlphaBlend options (time, curve, etc.) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blend", meta = (DisplayAfter = "BlendMode"))
	FAlphaBlendArgs Blend;

	/** Type of blend mode (Standard vs Inertial) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blend")
	EAnimNextPlayAnimBlendMode BlendMode = EAnimNextPlayAnimBlendMode::Standard;
};

/**
 * PlayAnim payload
 *
 * Encapsulates the data interface used to play a simple animation
 */
USTRUCT(BlueprintType)
struct FAnimNextPlayAnimPayload
{
	GENERATED_BODY()

	// The animation object to play with this request
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayAnim")
	TObjectPtr<UAnimSequence> AnimationObject;

	// The play rate of the request
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayAnim")
	double PlayRate = 1.0f;

	// The timeline start position of the request
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayAnim")
	double StartPosition = 0.0f;

	// Whether to loop the animation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayAnim")
	bool IsLooping = false;
};

/**
 * PlayAnim Request Arguments
 *
 * Encapsulates the parameters required to initiate a Play animation request.
 */
USTRUCT()
struct FAnimNextPlayAnimRequestArgs
{
	GENERATED_BODY()

	// The slot name to play the animation object on with this request
	UPROPERTY()
	FName SlotName;

	// The blend settings to use when blending in
	UPROPERTY()
	FAnimNextPlayAnimBlendSettings BlendInSettings;

	// The blend settings to use when blending out (if not interrupted)
	UPROPERTY()
	FAnimNextPlayAnimBlendSettings BlendOutSettings;

	// Object to 'play'.
	// The animation graph to be instantiated for this request will be chosen by interrogating this object's class.
	UPROPERTY()
	TObjectPtr<UObject> Object;

	// Payload that will be applied to the animation graph's variables via its data interfaces.
	UPROPERTY()
	FInstancedStruct Payload;
};

namespace UE::AnimNext
{
	// Create a namespaced aliases to simplify usage
	using EPlayAnimBlendMode = EAnimNextPlayAnimBlendMode;
	using FPlayAnimBlendSettings = FAnimNextPlayAnimBlendSettings;
	using FPlayAnimRequestArgs = FAnimNextPlayAnimRequestArgs;

	struct FPlayAnimRequest;
	DECLARE_DELEGATE_OneParam(FAnimNextOnPlayAnimStarted, const FPlayAnimRequest&)
	DECLARE_DELEGATE_OneParam(FAnimNextOnPlayAnimCompleted, const FPlayAnimRequest&)
	DECLARE_DELEGATE_OneParam(FAnimNextOnPlayAnimInterrupted, const FPlayAnimRequest&)
	DECLARE_DELEGATE_OneParam(FAnimNextOnPlayAnimBlendingOut, const FPlayAnimRequest&)

	/**
	 * PlayAnim Request
	 *
	 * Instances of this class represent individual requests to the PlayAnim system.
	 * They are allocated as shared pointers and ownership is split between gameplay (until
	 * it no longer cares about a particular request) and the animation slot that plays it
	 * (until the request completes).
	 * 
	 * Use MakePlayAnimRequest(...) to construct instances of this type.
	 */
	struct FPlayAnimRequest : public TSharedFromThis<FPlayAnimRequest, ESPMode::ThreadSafe>
	{
		// Callback called when the request starts playing (status transitions from pending to playing)
		FAnimNextOnPlayAnimStarted OnStarted;

		// Callback called when the request completes (status transitions from playing to completed)
		FAnimNextOnPlayAnimCompleted OnCompleted;

		// Callback called when the request is interrupted (either by calling Stop on it or by another request)
		FAnimNextOnPlayAnimInterrupted OnInterrupted;

		// Callback called when the request starts blending out (if it wasn't interrupted)
		FAnimNextOnPlayAnimBlendingOut OnBlendingOut;

		// Sends this request to the specified component and it will attempt to play with the requested arguments
		bool Play(FPlayAnimRequestArgs&& InRequestArgs, UAnimNextComponent* InComponent);

		// Interrupts this request and request that we transition to the source input on the playing slot
		void Stop();

		// Returns the arguments this request is playing
		const FPlayAnimRequestArgs& GetArgs() const;

		// Returns the arguments this request is playing
		FPlayAnimRequestArgs& GetMutableArgs();

		// Returns the request status
		EPlayAnimStatus GetStatus() const;

		// Returns the current timeline progress
		FTimelineProgress GetTimelineProgress() const;

		// Returns whether or not this request has expired
		bool HasExpired() const;

		// Returns whether or not this request has completed (might have been interrupted)
		bool HasCompleted() const;

		// Returns whether or not this request is playing (might be blending out or interrupted)
		bool IsPlaying() const;

		// Returns whether or not this request is blending out
		bool IsBlendingOut() const;

		// Returns whether or not this request was interrupted (by Stop or by another request)
		bool WasInterrupted() const;

		// GC API
		void AddReferencedObjects(FReferenceCollector& Collector);

	private:
		void OnStatusUpdate(EPlayAnimStatus NewStatus);
		void OnTimelineUpdate(FTimelineProgress NewTimelineProgress);

		// The request arguments
		FPlayAnimRequestArgs RequestArgs;

		// The component we are playing on
		TObjectPtr<UAnimNextComponent> Component;

		// The pending start event if we haven't started playing yet
		FAnimNextTraitEventPtr PendingStartEvent;

		// The current request status
		EPlayAnimStatus Status = EPlayAnimStatus::None;

		// The current timeline progress
		FTimelineProgress TimelineProgress;

		friend FPlayAnim_PlayEvent;
		friend FPlayAnim_StatusUpdateEvent;
		friend FPlayAnim_TimelineUpdateEvent;
	};

	// Create a shared pointer alias for PlayAnim requests
	using FPlayAnimRequestPtr = TSharedPtr<FPlayAnimRequest, ESPMode::ThreadSafe>;

	// Constructs a PlayAnim request object
	inline FPlayAnimRequestPtr MakePlayAnimRequest()
	{
		return MakeShared<FPlayAnimRequest>();
	}
}
