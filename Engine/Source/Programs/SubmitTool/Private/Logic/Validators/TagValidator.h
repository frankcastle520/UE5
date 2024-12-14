// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ValidatorBase.h"
struct FUserData;
class FTag;

class FTagValidator : public FValidatorBase
{
public:
	using FValidatorBase::FValidatorBase;

	virtual bool Validate(const FString& InCLDescription, const TArray<FSourceControlStateRef>& InFilteredFilesInCL, const TArray<const FTag*>& InTags) override;
	virtual const FString& GetValidatorTypeName() const override { return SubmitToolParseConstants::TagValidator; }

protected:
	bool ValidateTag(const FTag* InTag, const TArray<TSharedPtr<FUserData>>& InP4Users, const TArray<TSharedPtr<FString>>& InP4Groups) const;

private:
	TArray<const FTag*> P4UserTags;
};
