// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeConsiderationBase.h"
#include "StateTreeTypes.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(StateTreeConsiderationBase)

FStateTreeConsiderationBase::FStateTreeConsiderationBase()
	: Operand(EStateTreeExpressionOperand::And)
	, DeltaIndent(0)
{
}

float FStateTreeConsiderationBase::GetNormalizedScore(FStateTreeExecutionContext& Context) const
{
	return FMath::Clamp(GetScore(Context), 0.f, 1.f);
}
