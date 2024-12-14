// Copyright Epic Games, Inc. All Rights Reserved.

#include "Configuration.h"

#include "Logging/SubmitToolLog.h"
#include "Misc/Paths.h"
#include "SubmitToolUtils.h"
#include "CommandLine/CmdLineParameters.h"

TSharedPtr<FConfiguration> FConfiguration::Instance;

void FConfiguration::Init()
{
	FConfiguration Configuration;

	FString RootDir;
	FCmdLineParameters::Get().GetValue(FSubmitToolCmdLine::RootDir, RootDir);
	if (RootDir.Len() == 0)
	{
		// fallback to one up from engine dir if nothing was passed in
		RootDir = FPaths::ConvertRelativePathToFull(FPaths::EngineDir() + TEXT(".."));
	}

	FPaths::NormalizeDirectoryName(RootDir);

	Configuration.Values.Add(TEXT("$(root)"), RootDir);

	FString Changelist;
	FCmdLineParameters::Get().GetValue(FSubmitToolCmdLine::P4ChangeList, Changelist);
	Configuration.Values.Add(TEXT("$(CL)"), Changelist);

	FString PerforceServerAndPort;
	FCmdLineParameters::Get().GetValue(FSubmitToolCmdLine::P4Server, PerforceServerAndPort);
	Configuration.Values.Add(TEXT("$(SERVER)"), PerforceServerAndPort);

	FString PerforceUserName;
	FCmdLineParameters::Get().GetValue(FSubmitToolCmdLine::P4User, PerforceUserName);
	Configuration.Values.Add(TEXT("$(USER)"), PerforceUserName);

	FString PerforceClientName;
	FCmdLineParameters::Get().GetValue(FSubmitToolCmdLine::P4Client, PerforceClientName);
	Configuration.Values.Add(TEXT("$(CLIENT)"), PerforceClientName);

	Configuration.Values.Add(TEXT("$(localappdata)"), *FSubmitToolUtils::GetLocalAppDataPath());

	Configuration.Values.Add(TEXT("$(SubmitToolSavedDir)"), *FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()));

	Configuration.Values.Add(TEXT("$(SubmitToolLogsDir)"), *FPaths::ConvertRelativePathToFull(FPaths::ProjectLogDir()));

	Configuration.Values.Add(TEXT("$(SubmitToolConfigDir)"), *FPaths::ConvertRelativePathToFull(FPaths::ProjectConfigDir()));

	Instance = MakeShared<FConfiguration>(Configuration);
}

void FConfiguration::AddOrUpdateEntry(const FString& Key, const FString& NewValue)
{
	if (Instance.IsValid())
	{
		if(Instance->Values.Contains(Key))
		{
			UE_LOG(LogSubmitToolDebug, Log, TEXT("Configuration succesfully updated entry with key %s, to %s"), *Key, *NewValue);
			Instance->Values[Key] = NewValue;
		}
		else
		{
			UE_LOG(LogSubmitToolDebug, Log, TEXT("Configuration succesfully added entry with key %s, to %s"), *Key, *NewValue);
			Instance->Values.Add(Key, NewValue);
		}
	}
	else
	{
		UE_LOG(LogSubmitTool, Log, TEXT("Configuration failed to update entry with key %s"), *Key);
	}
}

FString FConfiguration::Substitute(const FString& InStr)
{
	if (!Instance.IsValid())
	{
		return InStr;
	}

	FString Output(InStr);

	for (const TPair<FString, FString>& Pair : Instance->Values)
	{
		Output.ReplaceInline(*Pair.Key, *Pair.Value, ESearchCase::IgnoreCase);
	}

	return Output;
}


FString FConfiguration::SubstituteAndNormalizeFilename(const FString& InStr)
{
	FString Output = Substitute(InStr);

	FPaths::NormalizeFilename(Output);

	return Output;
}

FString FConfiguration::SubstituteAndNormalizeDirectory(const FString& InStr)
{
	FString Output = Substitute(InStr);

	FPaths::NormalizeDirectoryName(Output);

	return Output;
}
