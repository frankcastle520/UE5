// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ValidatorBase.h"
#include "Models/PreflightData.h"

class FPreflightService;

class FPreflightValidator : public FValidatorBase
{
public:
	using FValidatorBase::FValidatorBase;

	virtual bool Activate() override;

	virtual bool Validate(const FString& InCLDescription, const TArray<FSourceControlStateRef>& InFilteredFilesInCL, const TArray<const FTag*>& InTags) override;
	virtual const FString& GetValidatorTypeName() const override { return SubmitToolParseConstants::PreflightValidator; }
protected:
	void ValidatePreflights(const TUniquePtr<FPreflightList>& InPreflightList, const TMap<FString, FPreflightData>& InUnlinkedPreflights);
	virtual void Skip() override;

	void RemoveCallbacks();

	FTag* PreflightTag; 

	virtual void ValidationFinished(bool bSuccess) override;
	FDelegateHandle PreflightUpdateHandler;
	FDelegateHandle TagUpdateHandler;
};