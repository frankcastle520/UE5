// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditableDaySequenceConditionSet.h"

UEditableDaySequenceConditionSet::UEditableDaySequenceConditionSet()
: ConditionsProperty(nullptr)
{
	if (!IsTemplate())
	{
		ConditionsProperty = FindFProperty<FProperty>(GetClass(), TEXT("ConditionSet"));
	}
}

void UEditableDaySequenceConditionSet::SetConditions(const FDaySequenceConditionSet::FConditionValueMap& InConditions)
{
	ConditionSet.Conditions = InConditions;
}

FDaySequenceConditionSet::FConditionValueMap& UEditableDaySequenceConditionSet::GetConditions()
{
	return ConditionSet.Conditions;
}
	
FString UEditableDaySequenceConditionSet::GetConditionSetExportText()
{
	ConditionsPropertyAsString.Reset();
		
	if (ConditionsProperty)
	{
		ConditionsProperty->ExportTextItem_Direct(ConditionsPropertyAsString, &ConditionSet, &ConditionSet, this, 0);
	}

	return ConditionsPropertyAsString;
}