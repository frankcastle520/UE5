// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CmdLineParameter.h"

namespace FSubmitToolCmdLine
{
	const FString P4Server = TEXT("server");
	const FString P4Client = TEXT("client");
	const FString P4User = TEXT("user");
	const FString P4ChangeList = TEXT("cl");
	const FString RootDir = TEXT("root-dir");
	const FString ParameterFile = TEXT("param-file");

	const TArray<TSharedPtr<FCmdLineParameter>> SubmitToolCmdLineArgs =
	{
		MakeShared<FCmdLineParameter>(P4Server, true, TEXT("Perforce Server information, expected with format '<address>:<port>'")),
		MakeShared<FCmdLineParameter>(P4Client, true, TEXT("Perforce workspace name.")),
		MakeShared<FCmdLineParameter>(P4User, true, TEXT("Perforce user name.")),
		MakeShared<FCmdLineParameter>(P4ChangeList, true, TEXT("Perforce changelist number to submit."), [](const FString& InValue) { return InValue.IsNumeric() || InValue.Equals(TEXT("default")); }),
		MakeShared<FCmdLineParameter>(RootDir, false, TEXT("Root directory for the branch this change is part of"), nullptr,
		[](FString& OutValue)
		{
			TArray<TCHAR> CharactersToRemove{ '\\', '\"' , '\''};

			size_t Left = 0;
			size_t Right = OutValue.Len() - 1;
			bool bKeepLooping = true;

			while (bKeepLooping)
			{
				bKeepLooping = false;

				if (CharactersToRemove.Contains(OutValue[Left]))
				{
					Left++;
					bKeepLooping = true;
				}

				if (CharactersToRemove.Contains(OutValue[Right]))
				{
					Right--;
					bKeepLooping = true;
				}
			}
			OutValue.MidInline(Left, (Right - Left) + 1);
		}),
		MakeShared<FCmdLineParameter>(ParameterFile, false, TEXT("Submit tool parameters XML override.")),
	};
}

class FCmdLineParameters
{
public:
	FCmdLineParameters();
	bool ValidateParameters() const;
	void LogParameters() const;

	const static FCmdLineParameters& Get();

	bool Contains(const FString& InKey) const;
	bool GetValue(const FString& InKey, FString& OutValue) const;
private:
	const static FCmdLineParameters Instance;

	const TArray<TSharedPtr<FCmdLineParameter>> Parameters;
};