// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/DateTime.h"
#include "Misc/Timespan.h"
#include "PropertyAnimatorCoreTimeSourceBase.h"
#include "PropertyAnimatorCoreSystemTimeSource.generated.h"

/** Enumerates all possible modes for the machine clock time source */
UENUM(BlueprintType)
enum class EPropertyAnimatorCoreSystemMode : uint8
{
	/** Local time of the machine */
	LocalTime,
	/** Universal time = Greenwich Mean Time */
	UtcTime,
	/** Specified duration elapsing until it reaches 0 */
	Countdown,
	/** Current time elapsed since the time source is active */
	Stopwatch
};

/** System time source that support various option */
UCLASS(MinimalAPI)
class UPropertyAnimatorCoreSystemTimeSource : public UPropertyAnimatorCoreTimeSourceBase
{
	GENERATED_BODY()

public:
	UPropertyAnimatorCoreSystemTimeSource()
		: UPropertyAnimatorCoreTimeSourceBase(TEXT("System"))
	{}

	void SetMode(EPropertyAnimatorCoreSystemMode InMode);
	EPropertyAnimatorCoreSystemMode GetMode() const
	{
		return Mode;
	}

	void SetCountdownDuration(const FTimespan& InTimeSpan);
	void GetCountdownDuration(FTimespan& OutTimeSpan) const
	{
		OutTimeSpan = CountdownTimeSpan;
	}

	void SetCountdownDuration(const FString& InDuration);
	const FString& GetCountdownDuration() const
	{
		return CountdownDuration;
	}

protected:
	static FTimespan ParseTime(const FString& InFormat);

	//~ Begin UObject
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& InPropertyChangedEvent) override;
#endif
	//~ End UObject

	//~ Begin UPropertyAnimatorTimeSourceBase
	virtual bool UpdateEvaluationData(FPropertyAnimatorCoreTimeSourceEvaluationData& OutData) override;
	virtual void OnTimeSourceActive() override;
	virtual bool ImportPreset(const UPropertyAnimatorCorePresetBase* InPreset, const TSharedRef<FPropertyAnimatorCorePresetArchive>& InValue) override;
	virtual bool ExportPreset(const UPropertyAnimatorCorePresetBase* InPreset, TSharedPtr<FPropertyAnimatorCorePresetArchive>& OutValue) const override;
	//~ End UPropertyAnimatorTimeSourceBase

	void OnModeChanged();

	/** Machine time mode to use */
	UPROPERTY(EditInstanceOnly, Setter, Getter, Category="Animator")
	EPropertyAnimatorCoreSystemMode Mode = EPropertyAnimatorCoreSystemMode::LocalTime;

	/**
	* Countdown duration format : 
	* 120 = 2 minutes
	* 02:00 = 2 minutes
	* 00:02:00 = 2 minutes
	* 2m = 2 minutes
	* 1h = 1 hour
	* 120s = 2 minutes
	*/
	UPROPERTY(EditInstanceOnly, Setter, Getter, Category="Animator", meta=(EditCondition="Mode == EPropertyAnimatorCoreSystemMode::Countdown", EditConditionHides))
	FString CountdownDuration = TEXT("1m");

private:
	UPROPERTY(Transient)
	FTimespan CountdownTimeSpan;

	UPROPERTY(Transient)
	FDateTime ActivationTime;
};