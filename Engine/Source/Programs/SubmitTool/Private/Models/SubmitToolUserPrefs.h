// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Logging/SubmitToolLog.h"
#include "SubmitToolUserPrefs.generated.h"

USTRUCT()
struct FSubmitToolUserPrefs
{
	GENERATED_BODY()

	UPROPERTY()
	TMap<FString, FString> ValidatorOptions;
	
	UPROPERTY()
	bool bCloseOnSubmit = true;
	
	UPROPERTY()
	bool bExpandFilesInCL = false;

	UPROPERTY()
	FVector2D WindowPosition = FVector2D();

	UPROPERTY()
	FVector2D WindowSize = FVector2D();
	
	UPROPERTY()
	float P4SectionSize = 34;
	
	UPROPERTY()
	float ValidatorSectionSize = 22;
	
	UPROPERTY()
	float LogSectionSize = 46;

	UPROPERTY()
	TMap<FName, bool> UISectionExpandState =
	{};

	virtual ~FSubmitToolUserPrefs();

	static FSubmitToolUserPrefs* Get()
	{
		if(Instance == nullptr)
		{
			UE_LOG(LogSubmitTool, Error, TEXT("SubmitToolUserPrefs is not valid"));
		}

		return Instance;
	}

	static TUniquePtr<FSubmitToolUserPrefs> Initialize(const FString& InFilePath);
	private:

	static FString FilePath;
	static FSubmitToolUserPrefs* Instance;
};
