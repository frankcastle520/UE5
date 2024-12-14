// Copyright Epic Games, Inc. All Rights Reserved.

#include "expressions/Input/TG_Expression_Color.h"

void UTG_Expression_Color::Evaluate(FTG_EvaluationContext* InContext)
{
	Super::Evaluate(InContext);
	
	ValueOut = Color;
}

void UTG_Expression_Color::SetTitleName(FName NewName)
{
	GetParentNode()->GetInputPin("Color")->SetAliasName(NewName);
}

FName UTG_Expression_Color::GetTitleName() const
{
	return GetParentNode()->GetInputPin("Color")->GetAliasName();
}