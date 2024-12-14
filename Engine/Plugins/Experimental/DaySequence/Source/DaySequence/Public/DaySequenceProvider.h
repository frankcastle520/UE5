// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "DaySequenceProvider.generated.h"

class UDaySequence;

UCLASS()
class ADaySequenceProvider : public AActor
{
	GENERATED_BODY()
public:
	ADaySequenceProvider(const FObjectInitializer& Init);
	
	virtual TArrayView<TObjectPtr<UDaySequence>> GetDaySequences();

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif //WITH_EDITOR

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sequence", meta=(AllowedClasses="/Script/DaySequence.DaySequence"))
	TArray<TObjectPtr<UDaySequence>> DaySequenceAssets;
};
