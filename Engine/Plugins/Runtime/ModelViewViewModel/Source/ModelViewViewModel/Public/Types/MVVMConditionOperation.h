// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MVVMConditionOperation.generated.h"


/** */
UENUM(BlueprintType)
enum class EMVVMConditionOperation : uint8
{
	Equal,
	NotEqual,
	MoreThan,
	MoreThanOrEqual,
	LessThan,
	LessThanOrEqual,
	BetweenInclusive,
	BetweenExclusive
};

MODELVIEWVIEWMODEL_API const TCHAR* LexToString(EMVVMConditionOperation Enum);


#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_2
#include "CoreMinimal.h"
#endif
