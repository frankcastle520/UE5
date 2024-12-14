// Copyright Epic Games, Inc. All Rights Reserved.

#include "DaySequenceConditionTag.h"

void UDaySequenceConditionTag::Initialize()
{
	SetupOnConditionValueChanged();
}

bool UDaySequenceConditionTag::Evaluate_Implementation() const
{
	return true;
}

FString UDaySequenceConditionTag::GetConditionName() const
{
	return ConditionName.IsEmpty() ? GetClass()->GetName() : ConditionName;
}

UWorld* UDaySequenceConditionTag::GetWorld() const
{
	if (IsTemplate())
	{
		return nullptr;
	}

	return GetOuter()->GetWorld();
}

void UDaySequenceConditionTag::SetupOnConditionValueChanged_Implementation() const
{
}

void UDaySequenceConditionTag::BroadcastOnConditionValueChanged()
{
	const bool bResult = Evaluate();
	if (!bCachedEvalResult.IsSet() || bCachedEvalResult.GetValue() != bResult)
	{
		bCachedEvalResult = bResult;
		OnConditionValueChanged.Broadcast();
	}
}