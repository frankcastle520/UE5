// Copyright Epic Games, Inc. All Rights Reserved.

#include "expressions/Input/TG_Expression_Scalar.h"


void UTG_Expression_Scalar::Evaluate(FTG_EvaluationContext* InContext)
{
	Super::Evaluate(InContext);

	// The Value is updated either as an input or as a setting and then becomes the output for this expression
	// The pin out is named "ValueOut"
	ValueOut = Scalar;
}

void UTG_Expression_Scalar::SetTitleName(FName NewName)
{
	GetParentNode()->GetInputPin(GET_MEMBER_NAME_CHECKED(UTG_Expression_Scalar, Scalar))->SetAliasName(NewName);
}

FName UTG_Expression_Scalar::GetTitleName() const
{
	return GetParentNode()->GetInputPin(GET_MEMBER_NAME_CHECKED(UTG_Expression_Scalar, Scalar))->GetAliasName();
}