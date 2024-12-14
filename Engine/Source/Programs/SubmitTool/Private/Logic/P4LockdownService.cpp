// Copyright Epic Games, Inc. All Rights Reserved.

#include "P4LockdownService.h"
#include "CommandLine/CmdLineParameters.h"
#include "Logic/Services/Interfaces/ISTSourceControlService.h"
#include "Logic/Services/SubmitToolServiceProvider.h"
#include "Logging/SubmitToolLog.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Internationalization/Regex.h"
#include "Configuration/Configuration.h"

FP4LockdownService::FP4LockdownService(const FP4LockdownParameters& InParameters, TSharedPtr<FSubmitToolServiceProvider> InServiceProvider)
: 	ServiceProvider(InServiceProvider), Parameters(InParameters)
{
	using namespace UE::Tasks;
	ConfigFilesTask = Launch(UE_SOURCE_LOCATION, [this]
		{
			TArray<TTask<bool>> DownloadFileTasks;
			for(const TPair<FString, FString>& Pair : Parameters.ConfigPaths)
			{
				DownloadFileTasks.Add(Launch(UE_SOURCE_LOCATION, [this, &Pair]
					{
						return GetConfigFile(Pair.Key, Pair.Value);
					}));
			}

			Wait(DownloadFileTasks);

			bool bResult = true;
			for(TTask<bool>& Task : DownloadFileTasks)
			{
				bResult &= Task.GetResult();
			}

			return bResult;
		});
}

bool FP4LockdownService::ArePathsInLockdown(const TArray<FString>& InPaths, bool& bOutAllowlisted)
{	
	if(!ConfigFilesTask.IsValid())
	{
		UE_LOG(LogSubmitToolP4, Error, TEXT("Downloading task wasn't setup correctly, hardlock status is not known."));
		return false;
	}

	if(!ConfigFilesTask.IsCompleted())
	{		
		UE_LOG(LogSubmitToolP4, Log, TEXT("Waiting for download of Stream Hardlock data..."));
		ConfigFilesTask.Wait(FTimespan::FromSeconds(5));

		if(!ConfigFilesTask.IsCompleted())
		{
			UE_LOG(LogSubmitToolP4, Error, TEXT("Downloading config files from P4 timed out, hardlock status is not latest, will use cache."));
		}
	}

	if(ConfigFilesTask.IsCompleted())
	{
		for(const TPair<FString, FSharedBuffer>& Pair : DownloadedFiles)
		{
			const FString Path = GetFilePath(Pair.Key);
			FArchive* File = IFileManager::Get().CreateFileWriter(*Path, FILEWRITE_EvenIfReadOnly);

			if(File != nullptr)
			{
				File->Serialize(const_cast<void*>(Pair.Value.GetData()), Pair.Value.GetSize());
				File->Close();
				delete File;
			}
			else
			{
				UE_LOG(LogSubmitTool, Warning, TEXT("Couldn't create lockdown file %s"), *Path)
			}
		}
	}

	if(!ConfigFilesTask.GetResult())
	{
		UE_LOG(LogSubmitToolP4, Warning, TEXT("Downloading config files from P4 had failures. Lockdown data won't be complete"));
	}

	ParseAllowListData();

	if(InPaths.IsEmpty())
	{
		UE_LOG(LogSubmitToolP4, Warning, TEXT("No files to check for lockdown"));
	}

	bool bLockdown = false;
	bOutAllowlisted = true;
	for(const FString& Path : InPaths)
	{
		FPathLockdownResult Result = IsPathInLockdown(Path);
		bLockdown |= Result.bIsLocked;

		if(Result.bIsLocked)
		{
			bOutAllowlisted &= Result.bAllowlisted;
		}
	}

	return bLockdown;
}

FP4LockdownService::FPathLockdownResult FP4LockdownService::IsPathInLockdown(const FString& InPath) const
{
	FString PerforceUserName;
	FCmdLineParameters::Get().GetValue(FSubmitToolCmdLine::P4User, PerforceUserName);

	FPathLockdownResult bOverallLockdownResult { false, true };

	for(const FAllowListData& data : AllowListData)
	{
		bool bIsLocked = false;

		auto EvaluateViews = [&bIsLocked, &InPath](const TArray<TPair<bool, FString>>& Views)
		{
			for (const TPair<bool, FString>& View : Views)
			{
				if (bIsLocked != View.Key)
				{
					FRegexPattern Pattern = FRegexPattern(View.Value, ERegexPatternFlags::CaseInsensitive);
					FRegexMatcher regex = FRegexMatcher(MoveTemp(Pattern), InPath);

					if (regex.FindNext())
					{
						bIsLocked = View.Key;
					}
				}
			}
		};
		EvaluateViews(data.Views);

		bool bIsInOverrideAllowlist = false;
		for (const FOverrideData& Override : OverrideData)
		{
			if (Override.Sections.Contains(data.GroupName) && Override.AllowListers.Contains(PerforceUserName))
			{
				EvaluateViews(Override.Views);
				bIsInOverrideAllowlist = true;
			}
		}

		if (bIsLocked)
		{
			bOverallLockdownResult.bAllowlisted = bIsInOverrideAllowlist || data.AllowListers.Contains(PerforceUserName);
			bOverallLockdownResult.bIsLocked = true;
			break;
		}
	}

	for(const FString& AdditionalHardlockedPath : AdditionalHardlocks)
	{
		FRegexPattern Pattern = FRegexPattern(AdditionalHardlockedPath, ERegexPatternFlags::CaseInsensitive);
		FRegexMatcher regex = FRegexMatcher(MoveTemp(Pattern), InPath);

		if(regex.FindNext())
		{
			bOverallLockdownResult.bIsLocked = true;
			bOverallLockdownResult.bAllowlisted = false;
			break;
		}
	}

	return bOverallLockdownResult;
}

bool FP4LockdownService::GetConfigFile(const FString& InConfigId, const FString& InDepotPath)
{
	TArray<FSharedBuffer> FileBuffers;
	if(ServiceProvider->GetService<ISTSourceControlService>()->DownloadFiles(InDepotPath, FileBuffers).GetResult() && FileBuffers.Num() > 0)
	{
		FScopeLock Scope(&FileMutex);
		
		DownloadedFiles.Add(InConfigId, FileBuffers[0]);
		return true;
	}

	return false;
}

void RegexEscapeInline(FString& InOutRegex)
{
	for (int i = InOutRegex.Len() - 1; i >= 0; --i)
	{
		switch (InOutRegex[i])
		{
			case TEXT('\\'):
			case TEXT('*'):
			case TEXT('+'):
			case TEXT('?'):
			case TEXT('|'):
			case TEXT('{'):
			case TEXT('}'):
			case TEXT('['):
			case TEXT(']'):
			case TEXT('('):
			case TEXT(')'):
			case TEXT('^'):
			case TEXT('$'):
			case TEXT('.'):
			case TEXT('#'):
			case TEXT(' '):
				InOutRegex.InsertAt(i, TEXT('\\'));
				break;
		}
	}
}

void FP4LockdownService::GetAdditionalHardlockedPaths()
{
	for(FString EscapedViewLine : Parameters.AdditionalHardlockedPaths)
	{
		RegexEscapeInline(EscapedViewLine);
		EscapedViewLine.ReplaceInline(TEXT("\\*"), TEXT("[^/]*"));
		EscapedViewLine.ReplaceInline(TEXT("\\.\\.\\."), TEXT(".*"));

		AdditionalHardlocks.Add(MoveTemp(EscapedViewLine));
	}
}

void FP4LockdownService::ParseAllowListData()
{
	for(const TPair<FString, FString>& Config : Parameters.ConfigPaths)
	{
		FString Filepath = GetFilePath(Config.Key);
		if(IFileManager::Get().FileExists(*Filepath))
		{
			FConfigFile LockdownConfig;
			LockdownConfig.bPythonConfigParserMode = true;
			LockdownConfig.Read(Filepath);

			for (const TPair<FString, FConfigSection>& ConfigPair : AsConst(LockdownConfig))
			{
				const FConfigSection& ConfigSection = ConfigPair.Value;
				
				const FConfigValue* AllowList = ConfigSection.Find(TEXT("allowlist"));
				if (AllowList == nullptr)
				{
					continue;
				}

				const FConfigValue* Status = ConfigSection.Find(TEXT("status"));
				if (Status == nullptr)
				{
					continue;
				}

				FAllowListData* data;
				if (Status->GetSavedValue() == TEXT("hardcore"))
				{
					data = &AllowListData.AddDefaulted_GetRef();
				}
				else if (Status->GetSavedValue() == TEXT("override"))
				{
					const FConfigValue* SectionList = ConfigSection.Find(TEXT("sectionlist"));
					if (SectionList == nullptr)
					{
						continue;
					}

					FOverrideData& Override = OverrideData.AddDefaulted_GetRef();
					data = &Override;

					TArray<FString> Sections;
					SectionList->GetSavedValue().ParseIntoArray(Sections, TEXT(","), true);

					for(FString& Section : Sections)
					{
						Section.ToLowerInline();
						Override.Sections.Add(MoveTemp(Section));
					}
				}
				else
				{
					continue;
				}

				data->GroupName = ConfigPair.Key;

				TArray<FString> Allowlisters;
				AllowList->GetSavedValue().ParseIntoArray(Allowlisters, TEXT(","), true);

				for(FString& AllowLister : Allowlisters)
				{
					AllowLister.ToLowerInline();
					data->AllowListers.Add(MoveTemp(AllowLister));
				}

				TArray<FString> View;
				ConfigSection.MultiFind(TEXT("view"), View, true);
				for(FString& ViewLine : View)
				{
					if(ViewLine.IsEmpty())
					{
						continue;
					}

					bool bIsLocked = true;
					if(ViewLine.StartsWith(TEXT("-"), ESearchCase::CaseSensitive))
					{
						bIsLocked = false;
						ViewLine.MidInline(2);
					}

					RegexEscapeInline(ViewLine);
					ViewLine.ReplaceInline(TEXT("\\*"), TEXT("[^/]*"), ESearchCase::CaseSensitive);
					ViewLine.ReplaceInline(TEXT("\\.\\.\\."), TEXT(".*"), ESearchCase::CaseSensitive);

					data->Views.Add(TPair<bool, FString>(bIsLocked, MoveTemp(ViewLine)));
				}
			}
		}
		else
		{
			UE_LOG(LogSubmitToolP4Debug, Error, TEXT("File %s doesn't exist"), *Filepath);
		}
	}
}

FString FP4LockdownService::GetFilePath(const FString& InConfigId)
{
	FString EngineDir = FPaths::ConvertRelativePathToFull(FPaths::EngineDir());
	FPaths::NormalizeDirectoryName(EngineDir);
	
	const FGuid guid = FGuid::NewGuid();
	FString LocalFilePath = EngineDir + TEXT("/Intermediate/SubmitTool/P4Lockdown/") + InConfigId + TEXT(".ini");
	LocalFilePath = FPaths::ConvertRelativePathToFull(LocalFilePath);
	FPaths::NormalizeFilename(LocalFilePath);
		
	return LocalFilePath;
}