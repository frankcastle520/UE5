// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FCmdLineParameter
{
public:
	FCmdLineParameter() = delete;
	FCmdLineParameter(FString InKey, bool InIsRequired, FString InDescription, TFunction<bool(const FString& InValue)> InValidator = nullptr, TFunction<void(FString& InValue)> InParseInPlace = nullptr) :
		Key(InKey),
		bIsRequired(InIsRequired),
		Description(InDescription),
		Validator(InValidator),
		Parser(InParseInPlace)
	{}

	FString ParameterKey() const { return this->Key; }
	FString ParameterDescription() const { return this->Description; }
	bool IsRequired() const { return this->bIsRequired; }
	bool IsValid(const FString& value) const
	{
		if(Validator != nullptr)
		{
			return Validator(value);
		}

		return true;
	}

	void CustomParse(FString& InOutValue) const
	{
		if(Parser != nullptr)
		{
			Parser(InOutValue);
		}
	}


private:
	FString Key;
	bool bIsRequired;
	FString Description;
	TFunction<bool(const FString& InParam)> Validator;
	TFunction<void(FString& InParam)> Parser;
};