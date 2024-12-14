// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PropertyAnimatorCoreTimeSourceBase.h"
#include "PropertyAnimatorCoreManualTimeSource.generated.h"

UENUM()
enum class EPropertyAnimatorCoreManualStatus : uint8
{
	/** Animation is done */
	Stopped = 1 << 0,
	/** Animation is paused */
	Paused = 1 << 1,
	/** Animation is playing */
	PlayingForward = 1 << 2,
	/** Animation is playing in reverse */
	PlayingBackward = 1 << 3
};

/** Used to store status of player, for type customization */
USTRUCT()
struct FPropertyAnimatorCoreManualState
{
	GENERATED_BODY()

	UPROPERTY(EditInstanceOnly, Category="Animator")
	EPropertyAnimatorCoreManualStatus Status = EPropertyAnimatorCoreManualStatus::Stopped;
};

UCLASS(MinimalAPI)
class UPropertyAnimatorCoreManualTimeSource : public UPropertyAnimatorCoreTimeSourceBase
{
	GENERATED_BODY()

public:
	UPropertyAnimatorCoreManualTimeSource()
		: UPropertyAnimatorCoreTimeSourceBase(TEXT("Manual"))
	{}

	void SetOverrideTime(bool bInOverride);
	bool GetOverrideTime() const
	{
		return bOverrideTime;
	}

	PROPERTYANIMATORCORE_API void SetCustomTime(double InTime);
	double GetCustomTime() const
	{
		return CustomTime;
	}

	void SetState(const FPropertyAnimatorCoreManualState& InState);
	const FPropertyAnimatorCoreManualState& GetState() const
	{
		return State;
	}

	void Play(bool bInForward);
	void Pause();
	void Stop();

	EPropertyAnimatorCoreManualStatus GetPlaybackStatus() const;
	bool IsPlaying() const;

protected:
	//~ Begin UObject
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& InPropertyChangedEvent) override;
#endif
	//~ End UObject

	//~ Begin UPropertyAnimatorTimeSourceBase
	virtual bool UpdateEvaluationData(FPropertyAnimatorCoreTimeSourceEvaluationData& OutData) override;
	virtual void OnTimeSourceActive() override;
	virtual void OnTimeSourceInactive() override;
	virtual bool ImportPreset(const UPropertyAnimatorCorePresetBase* InPreset, const TSharedRef<FPropertyAnimatorCorePresetArchive>& InValue) override;
	virtual bool ExportPreset(const UPropertyAnimatorCorePresetBase* InPreset, TSharedPtr<FPropertyAnimatorCorePresetArchive>& OutValue) const override;
	//~ End UPropertyAnimatorTimeSourceBase

	void OnStateChanged();
	void OnOverrideTimeChanged();

	/** Allows you to drive controllers with this float */
	UPROPERTY(EditInstanceOnly, Setter="SetOverrideTime", Getter="GetOverrideTime", Category="Animator", meta=(InlineEditConditionToggle))
	bool bOverrideTime = true;

	/** Time to evaluate */
	UPROPERTY(EditInstanceOnly, Setter, Getter, Category="Animator", meta=(EditCondition="bOverrideTime", Units=Seconds))
	double CustomTime = 0.f;

	/** Playback state */
	UPROPERTY(EditInstanceOnly, Transient, DuplicateTransient, Setter, Getter, Category="Animator", meta=(EditCondition="!bOverrideTime", EditConditionHides, HideEditConditionToggles))
	FPropertyAnimatorCoreManualState State;

	/** Current active status for the player */
	EPropertyAnimatorCoreManualStatus ActiveStatus = EPropertyAnimatorCoreManualStatus::Stopped;
};