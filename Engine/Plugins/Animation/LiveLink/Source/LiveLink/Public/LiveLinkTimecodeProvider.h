// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/TimecodeProvider.h"

#include "LiveLinkTypes.h"
#include "LiveLinkTimecodeProvider.generated.h"

struct FPropertyChangedEvent;
struct FQualifiedFrameTime;

class ILiveLinkClient;
class IModularFeature;

UENUM()
enum class ELiveLinkTimecodeProviderEvaluationType
{
	/** Interpolate between, or extrapolate using the 2 frames that are the closest to the current world time. */
	Lerp,
	/** Use the frame that is closest to the current world time. */
	Nearest,
	/** Use the newest frame that was received. */
	Latest,
};

/**
 * Fetch the latest frames from the LiveLink subject and create a Timecode from it.
 */
UCLASS(config=Engine, Blueprintable, editinlinenew)
class LIVELINK_API ULiveLinkTimecodeProvider : public UTimecodeProvider
{
	GENERATED_BODY()

public:
	ULiveLinkTimecodeProvider();

	//~ Begin UTimecodeProvider Interface
	virtual FQualifiedFrameTime GetQualifiedFrameTime() const override;
	
	virtual ETimecodeProviderSynchronizationState GetSynchronizationState() const override
	{
		return State;
	}

	virtual bool Initialize(class UEngine* InEngine) override;
	virtual void Shutdown(class UEngine* InEngine) override;
	//~ End UTimecodeProvider Interface

	//~ Begin UObject Interface
	virtual void BeginDestroy() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~End UObject Interface

	/**
	 * Allows users to override the target timecode provider subject key. Live
	 * Link Hub will send subject name to the host but the host has to "lookup"
	 * and match it with the available subject keys on the local machine.
	 */
	void SetTargetSubjectKey(const FLiveLinkSubjectKey& InKey)
	{
		SubjectKey = InKey;
	}
private:
	FQualifiedFrameTime ConvertTo(FQualifiedFrameTime Value) const;

	/**
	 * Infers the frame time for the current world time by either interpolating between or extrapolating a frame time value from two subject frames provided via live link.
	 *
	 * This method uses the FMath::Lerp function but will intentionally provide an Alpha value greater than 1.0 when extrapolation is required.
	 * 
	 * @param Seconds The current world time.
	 * @param IndexA The first subject frame index.
	 * @param IndexB The second subject frame index.
	 * @return The inferred frame time.
	 */
	FQualifiedFrameTime LerpBetweenFrames(double Seconds, int32 IndexA, int32 IndexB) const;

	void InitClient();
	void UninitClient();
	void RegisterSubject();
	void UnregisterSubject();
	void OnLiveLinkClientRegistered(const FName& Type, class IModularFeature* ModularFeature);
	void OnLiveLinkClientUnregistered(const FName& Type, class IModularFeature* ModularFeature);
	void OnLiveLinkSubjectAdded(FLiveLinkSubjectKey SubjectKey);
	void OnLiveLinkSubjectRemoved(FLiveLinkSubjectKey SubjectKey);
	void OnLiveLinkFrameDataReceived_AnyThread(const FLiveLinkFrameDataStruct& FrameData);

private:
	/** The specific subject that we listen to. */
	UPROPERTY(EditAnywhere, Category = Timecode)
	FLiveLinkSubjectKey SubjectKey;

	/** How to evaluate the timecode. */
	UPROPERTY(EditAnywhere, Category = Timecode)
	ELiveLinkTimecodeProviderEvaluationType Evaluation;

	UPROPERTY(EditAnywhere, Category = Timecode, meta=(InlineEditConditionToggle))
	bool bOverrideFrameRate;

	/**
	 * Override the frame rate at which this timecode provider will create its timecode value.
	 * By default, we use the subject frame rate.
	 */
	UPROPERTY(EditAnywhere, Category = Timecode, meta=(EditCondition="bOverrideFrameRate"))
	FFrameRate OverrideFrameRate;

	/** The number of frame to keep in memory. The provider will not be synchronized until the buffer is full at least once. */
	UPROPERTY(AdvancedDisplay, EditAnywhere, Category = Timecode, meta=(ClampMin = "2", UIMin = "2", ClampMax = "10", UIMax = "10"))
	int32 BufferSize;

private:
	TAtomic<ETimecodeProviderSynchronizationState> State;
	ILiveLinkClient* LiveLinkClient;
	FLiveLinkSubjectKey RegisteredSubjectKey;
	TArray<FLiveLinkTime> SubjectFrameTimes;
	mutable FCriticalSection SubjectFrameLock; // Only lock SubjectFrameTimes
	FDelegateHandle RegisterForFrameDataReceivedHandle;
};
