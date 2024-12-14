// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Models/Tag.h"
#include "Parameters/SubmitToolParameters.h"
#include "ChangelistService.h"
#include "Services/Interfaces/ISubmitToolService.h"

class FTagService : public ISubmitToolService
{
public:
	FTagService(const FSubmitToolParameters& InParameters, TSharedPtr<FChangelistService> clService);

	void ParseCLDescription();

	void ApplyTag(const FString& tagID);
	void ApplyTag(FTag& tag);
	void UpdateTagsInCL();
	void RemoveTag(const FString& tagID);
	void RemoveTag(FTag& tag);
	void SetTagValues(const FString& tagID, const FString& values);
	void SetTagValues(FTag& tag, const FString& values);
	void SetTagValues(FTag& tag, const TArray<FString>& values);

	FTag* GetTag(const FString& tagID)
	{
		if(RegisteredTags.Contains(tagID))
		{
			return &RegisteredTags[tagID];
		}

		return nullptr;
	};

	FTagUpdated OnTagUpdated;

	const TArray<const FTag*>& GetTagsArray() const;

private:
	FString& GetCLDescription() { return const_cast<FString&>(ChangelistService->GetCLDescription()); }

	mutable TArray<const FTag*> CachedTags;

	const FSubmitToolParameters& Parameters;
	TMap<FString, FTag> RegisteredTags;
	TSharedPtr<FChangelistService> ChangelistService;

	void RegisterTags();
	void UpdateTagsPositions(size_t changePos, int32 delta);
};

Expose_TNameOf(FTagService);