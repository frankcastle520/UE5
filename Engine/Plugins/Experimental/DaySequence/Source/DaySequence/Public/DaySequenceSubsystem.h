// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "DaySequenceSubsystem.generated.h"

class UCheatManager;
class UDaySequenceCheatManagerExtension;

class ADaySequenceActor;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDaySequenceActorSet, ADaySequenceActor*, NewActor);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnDaySequenceActorSetEvent, ADaySequenceActor*);

UCLASS()
class DAYSEQUENCE_API UDaySequenceSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	/** UWorldSubsystem interface */
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category=DaySequence)
	ADaySequenceActor* GetDaySequenceActor(bool bFindFallbackOnNull = true) const;

	UFUNCTION(BlueprintCallable, Category=DaySequence)
	void SetDaySequenceActor(ADaySequenceActor* InActor);

	/** Blueprint exposed delegate that is broadcast when the active DaySequenceActor changes. */
	UPROPERTY(BlueprintAssignable, Category=DaySequence)
	FOnDaySequenceActorSet OnDaySequenceActorSet;

	/** Natively exposed delegate that is broadcast when the active DaySequenceActor changes. */
	FOnDaySequenceActorSetEvent OnDaySequenceActorSetEvent;

	void OnCheatManagerCreated(UCheatManager* InCheatManager);

private:
	/** Utility function that broadcasts both OnDaySequenceActorSet and OnDaySequenceActorSetEvent. */
	void BroadcastOnDaySequenceActorSet(ADaySequenceActor* InActor) const;
	
	TWeakObjectPtr<ADaySequenceActor> DaySequenceActor;

	TWeakObjectPtr<UDaySequenceCheatManagerExtension> CheatManagerExtension;
};
