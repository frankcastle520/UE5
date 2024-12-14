// Copyright Epic Games, Inc. All Rights Reserved.

#include "CmdLineParameters.h"
#include "Logging/SubmitToolLog.h"
#include "Misc/CommandLine.h"

const FCmdLineParameters FCmdLineParameters::Instance = FCmdLineParameters();

FCmdLineParameters::FCmdLineParameters()
	: Parameters(FSubmitToolCmdLine::SubmitToolCmdLineArgs)
{
}

bool FCmdLineParameters::ValidateParameters() const
{
	const TCHAR* CommandLine = FCommandLine::Get();

	bool bIsValid = true;
	for (const TSharedPtr<FCmdLineParameter>& Parameter : Parameters)
	{
		if (Parameter->IsRequired())
		{
			FString Key = Parameter->ParameterKey();
			if(!Key.EndsWith(TEXT(" ")))
			{
				Key += TEXT(" ");
			}

			FString Val;
			if (FParse::Value(CommandLine, *Key, Val))
			{
				if (Val.IsEmpty())
				{
					UE_LOG(LogSubmitTool, Error, TEXT("Command Line argument '-%s' has no value."), *Parameter->ParameterKey());
					bIsValid = false;
				}
				else if(!Parameter->IsValid(Val))
				{
					UE_LOG(LogSubmitTool, Error, TEXT("Command Line argument '-%s' value '%s' is invalid."), *Parameter->ParameterKey(), *Val);
					bIsValid = false;
				}
			}
			else
			{
				UE_LOG(LogSubmitTool, Error, TEXT("Command Line missing '-%s' argument."), *Parameter->ParameterKey());
				bIsValid = false;
			}
		}
		else
		{
			// optional parameters must contain a value if they are present in the command line
			FString Val;
			if (FParse::Value(CommandLine, *Parameter->ParameterKey(), Val))
			{
				if (Val.IsEmpty())
				{
					UE_LOG(LogSubmitTool, Error, TEXT("Command Line argument '-%s' has no value."), *Parameter->ParameterKey());
					bIsValid = false;
				}
			}
		}
	}

	return bIsValid;
}

void FCmdLineParameters::LogParameters() const
{
	for (const TSharedPtr<FCmdLineParameter>& Parameter : Parameters)
	{
		UE_LOG(LogSubmitTool, Warning, TEXT("-%s\t%s"), *Parameter->ParameterKey(), *Parameter->ParameterDescription());
	}
}

const FCmdLineParameters& FCmdLineParameters::Get()
{
	return Instance;
}

bool FCmdLineParameters::Contains(const FString& InKey) const
{
	return FParse::Param(FCommandLine::Get(), *InKey);
}

bool FCmdLineParameters::GetValue(const FString& InKey, FString& OutValue) const
{
	const TCHAR* CommandLine = FCommandLine::Get();

	TArray<FString> Tokens;
	TArray<FString> Switches;
	FCommandLine::Parse(CommandLine, Tokens, Switches);
	const TSharedPtr<FCmdLineParameter>* Definition = Parameters.FindByPredicate([InKey](const TSharedPtr<FCmdLineParameter>& InCmdParam) { return InCmdParam->ParameterKey() == InKey; });

	for(int i = 0; i < Tokens.Num(); i++)
	{
		if(Tokens[i].Equals(InKey, ESearchCase::IgnoreCase) && i + 1 < Tokens.Num())
		{
			OutValue = Tokens[i + 1];
			if(Definition)
			{
				(*Definition)->CustomParse(OutValue);
			}
			return true;
		}
	}

	for(int i = 0; i < Switches.Num(); i++)
	{
		FString Switch = Switches[i];

		if(Switch.StartsWith(InKey, ESearchCase::IgnoreCase))
		{
			TArray<FString> SplitSwitch;
			if(2 == Switch.ParseIntoArray(SplitSwitch, TEXT("="), true))
			{
				OutValue = SplitSwitch[1];
				if(Definition)
				{
					(*Definition)->CustomParse(OutValue);
				}
				return true;
			}
		}
	}

	return false;
}
