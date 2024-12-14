// Copyright Epic Games, Inc. All Rights Reserved.


#include "ValidatorRunExecutable.h"

#include "AnalyticsEventAttribute.h"
#include "CommandLine/CmdLineParameters.h"
#include "Configuration/Configuration.h"
#include "HAL/FileManagerGeneric.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/PathViews.h"
#include "Misc/Paths.h"
#include "Models/ModelInterface.h"
#include "Models/SubmitToolUserPrefs.h"
#include "Modules/BuildVersion.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

FValidatorRunExecutable::FValidatorRunExecutable(const FName& InNameId, const FSubmitToolParameters& InParameters, TSharedRef<FSubmitToolServiceProvider> InServiceProvider, const FString& InDefinition) :
	FValidatorBase(InNameId, InParameters, InServiceProvider, InDefinition)
{
	ParseDefinition(InDefinition);
}

void FValidatorRunExecutable::ParseDefinition(const FString& InDefinition)
{
	FStringOutputDevice Errors;
	Definition = MakeUnique<FValidatorRunExecutableDefinition>();
	FValidatorRunExecutableDefinition* ModifyableDefinition = const_cast<FValidatorRunExecutableDefinition*>(GetTypedDefinition<FValidatorRunExecutableDefinition>());
	FValidatorRunExecutableDefinition::StaticStruct()->ImportText(*InDefinition, ModifyableDefinition, nullptr, 0, &Errors, FValidatorRunExecutableDefinition::StaticStruct()->GetName());

	if(!Errors.IsEmpty())
	{
		UE_LOG(LogSubmitTool, Error, TEXT("Error loading parameter file %s"), *Errors);
		FModelInterface::SetErrorState();
	}
}

bool FValidatorRunExecutable::Validate(const FString& InCLDescription, const TArray<FSourceControlStateRef>& InFilteredFilesInCL, const TArray<const FTag*>& InTags)
{
	const FValidatorRunExecutableDefinition* TypedDefinition = GetTypedDefinition<FValidatorRunExecutableDefinition>();

	FString FinalArgs{ FConfiguration::Substitute(TypedDefinition->ExecutableArguments) };

	TArray<FString> Files;

	// aggregate all the valid files
	for(const FSourceControlStateRef& File : InFilteredFilesInCL)
	{
		Files.Add(File.Get().GetFilename());
	}

	if(!bIsValidSetup)
	{
		LogFailure(FString::Printf(TEXT("[%s] This task is not correctly setup and it's required for this change"), *ValidatorName));
		return false;
	}

	// create a file with the files and pass it as an argument of the validator (better to avoid breaking command line length limits)
	if(!TypedDefinition->FileListArgument.IsEmpty())
	{
		FString ValidatorDirectory = FPaths::EngineDir() + TEXT("Intermediate/SubmitTool/FileLists/");

		FGuid guid = FGuid::NewGuid();
		FString FileListPath = FPaths::ConvertRelativePathToFull(ValidatorDirectory + guid.ToString(EGuidFormats::DigitsWithHyphens) + TEXT(".txt"));

		FFileHelper::SaveStringArrayToFile(Files, *FileListPath, FFileHelper::EEncodingOptions::ForceAnsi, &IFileManager::Get(), EFileWrite::FILEWRITE_None);

		FinalArgs += FString::Printf(TEXT(" %s\"%s\""), *TypedDefinition->FileListArgument, *FileListPath);
	}

	// Legacy, pass down each file to the validator in the command line
	else if(!TypedDefinition->FileInCLArgument.IsEmpty())
	{
		for(const FString& File : Files)
		{
			FinalArgs += " " + TypedDefinition->FileInCLArgument + File;
		}
	}

	FString ExecutablePath;
	if(TypedDefinition->ExecutableCandidates.Num() != 0)
	{
		ExecutablePath = OptionsProvider.GetSelectedOptionValue(ExecutableOptions);
	}
	else
	{
		ExecutablePath = TypedDefinition->ExecutablePath;
	}

	return StartProcess(ExecutablePath, FinalArgs);
}

bool FValidatorRunExecutable::Activate()
{
	bIsValidSetup = FValidatorBase::Activate();

	PrepareExecutableOptions();
	const FValidatorRunExecutableDefinition* TypedDefinition = GetTypedDefinition<FValidatorRunExecutableDefinition>();

	FString SelectedOption = OptionsProvider.GetSelectedOptionKey(ExecutableOptions);
	if(TypedDefinition->ExecutableCandidates.Num() > 0 && SelectedOption.IsEmpty())
	{
		TArray<FString> ExecutablePaths;
		TypedDefinition->ExecutableCandidates.GenerateValueArray(ExecutablePaths);
		UE_LOG(LogSubmitTool, Error, TEXT("[%s] None of the executable candidates exists locally:\n%s"), *ValidatorName, *FString::Join(ExecutablePaths, TEXT("\n")));
		bIsValidSetup = false;
	}

	FValidatorRunExecutableDefinition* ModifyableDefinition = const_cast<FValidatorRunExecutableDefinition*>(TypedDefinition);

	ModifyableDefinition->ExecutablePath = FConfiguration::SubstituteAndNormalizeFilename(TypedDefinition->ExecutablePath);

	static TArray<FString> ValidExtensions =
	{
#if PLATFORM_WINDOWS
		TEXT(".exe"),
		TEXT(".bat"),
#elif PLATFORM_MAC
		TEXT(".app"),
		TEXT(".sh"),
		TEXT(".command"),
		TEXT(""),
#elif PLATFORM_LINUX
		TEXT(".sh"),
		TEXT(""),
#endif
	};

	if(TypedDefinition->ExecutablePath.IsEmpty())
	{
		for(const TTuple<FString,FString>& Paths : TypedDefinition->ExecutableCandidates)
		{
			if(!ValidExtensions.Contains(FPaths::GetExtension(Paths.Value, true)))
			{
				UE_LOG(LogSubmitTool, Error, TEXT("Task '%s' executable has an invalid extension for this platform: %s"), *ValidatorName, *Paths.Value);
				bIsValidSetup = false;
			}
		}

		if(TypedDefinition->ExecutableCandidates.Num() == 0)
		{
			UE_LOG(LogSubmitTool, Error, TEXT("Task '%s' does not have a value for 'ExecutablePath' or 'ExecutableCandidates'."), *ValidatorName);
			bIsValidSetup = false;
		}
	}
	else
	{
		if(!ValidExtensions.Contains(FPaths::GetExtension(TypedDefinition->ExecutablePath, true)))
		{
			UE_LOG(LogSubmitTool, Error, TEXT("Task '%s' executable has an invalid extension for this platform: %s"), *ValidatorName, *TypedDefinition->ExecutablePath);
			bIsValidSetup = false;
		}

		if(!FPaths::FileExists(TypedDefinition->ExecutablePath))
		{
			UE_LOG(LogSubmitTool, Error, TEXT("Task '%s' executable is not found on disk: %s."), *ValidatorName, *TypedDefinition->ExecutablePath);
			bIsValidSetup = false;
		}
	}

	if(!TypedDefinition->ExecutablePath.IsEmpty() && TypedDefinition->ExecutableCandidates.Num() > 0)
	{
		UE_LOG(LogSubmitTool, Error, TEXT("Specifying ExecutablePath and ExecutableCandidates for task %s is not supported, please check your config."), *GetValidatorName());
	}


	return bIsValidSetup;
}

void FValidatorRunExecutable::StopInternalValidations()
{
	if(GetValidatorState() == EValidationStates::Running)
	{
		if(ProcessWrapper.IsValid())
		{
			ProcessWrapper->Stop();
		}
	}
}

bool FValidatorRunExecutable::StartProcess(const FString& LocalPath, const FString& Args)
{
	// run validators from the $(root) dir
	FString WorkingDir = FConfiguration::Substitute(TEXT("$(root)"));
	ProcessWrapper = MakeUnique<FProcessWrapper>(GetValidatorName(), LocalPath, Args, FOnCompleted::CreateRaw(this, &FValidatorRunExecutable::OnProcessComplete), FOnOutputLine::CreateRaw(this, &FValidatorRunExecutable::OnProcessOutputLine), WorkingDir, GetTypedDefinition<FValidatorRunExecutableDefinition>()->bLaunchHidden, GetTypedDefinition<FValidatorRunExecutableDefinition>()->bLaunchReallyHidden);
	const bool ProcessStarted = ProcessWrapper->Start();
	bIgnoringOutputErrors = !GetTypedDefinition<FValidatorRunExecutableDefinition>()->EnableOutputErrorsAnchor.IsEmpty();

	if (ProcessStarted)
	{
		OnProcessOutputLine(FString(TEXT("Task process started.")), EProcessOutputType::ProcessInfo);
	}
	else
	{
		FString ErrorMessage = FString::Format(TEXT("Task process failed to start with Process path: '{0}' and arguments: '{1}'"),
			{
				*LocalPath,
				*Args
			});

		OnProcessOutputLine(ErrorMessage, EProcessOutputType::ProcessError);
		ValidationFinished(false);
	}

	return ProcessStarted;
}

void FValidatorRunExecutable::OnProcessOutputLine(const FString& Line, const EProcessOutputType& OutputType)
{
	if(OutputType == EProcessOutputType::ProcessError || IsLineAnError(Line))
	{
		LogFailure(FString::Printf(TEXT("[%s]: %s"), *GetValidatorName(), *Line));
	}
	else
	{
		UE_LOG(LogValidators, Log, TEXT("[%s]: %s"), *GetValidatorName(), *Line);
	}
}

bool FValidatorRunExecutable::IsLineAnError(const FString& InLine)
{
	const FValidatorRunExecutableDefinition* TypedDefinition = GetTypedDefinition<FValidatorRunExecutableDefinition>();

	if(!TypedDefinition->EnableOutputErrorsAnchor.IsEmpty() && InLine.Find(TypedDefinition->EnableOutputErrorsAnchor, ESearchCase::IgnoreCase) != INDEX_NONE)
	{
		bIgnoringOutputErrors = false;
		return false;
	}
	
	if(!TypedDefinition->DisableOutputErrorsAnchor.IsEmpty() && InLine.Find(TypedDefinition->DisableOutputErrorsAnchor, ESearchCase::IgnoreCase) != INDEX_NONE)
	{
		bIgnoringOutputErrors = true;
		return false;
	}

	if(bIgnoringOutputErrors)
	{
		return false;
	}

	for(const FString& Message : TypedDefinition->IgnoredErrorMessages)
	{
		if(InLine.Contains(Message, ESearchCase::IgnoreCase))
		{
			return false;
		}
	}

	for(const FString& Message : TypedDefinition->ErrorMessages)
	{
		if(InLine.Contains(Message, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}

	{
		FRegexPattern Pattern = FRegexPattern(TypedDefinition->RegexErrorParsing, ERegexPatternFlags::CaseInsensitive);
		FRegexMatcher Regex = FRegexMatcher(Pattern, InLine);
		if(Regex.FindNext())
		{
			return true;
		}
	}

	if(Definition->bTreatWarningsAsErrors)
	{
		FRegexPattern Pattern = FRegexPattern(TypedDefinition->RegexWarningParsing, ERegexPatternFlags::CaseInsensitive);
		FRegexMatcher Regex = FRegexMatcher(Pattern, InLine);
		if(Regex.FindNext())
		{
			return true;
		}
	}

	return false;
}

void FValidatorRunExecutable::OnProcessComplete(int32 ReturnCode)
{
	const FValidatorRunExecutableDefinition* Def = GetTypedDefinition<FValidatorRunExecutableDefinition>();
	const bool Success = Def->AllowedExitCodes.Contains(ReturnCode) && (Def->bOnlyLookAtExitCode || ErrorListCache.IsEmpty());

	if(Success)
	{
		UE_LOG(LogValidators, Log, TEXT("[%s]: Task process succeded (Exit code %d)"), *GetValidatorName(), ReturnCode);
	}
	else
	{
		LogFailure(FString::Printf(TEXT("[%s]: Task process failed with exit code %d and %d log errors."), *GetValidatorName(), ReturnCode, ErrorListCache.Num()));
	}

	ValidationFinished(Success);
}

const TArray<FAnalyticsEventAttribute> FValidatorRunExecutable::GetTelemetryAttributes() const
{
	TArray<FAnalyticsEventAttribute> Attributes = FValidatorBase::GetTelemetryAttributes();

	if (ProcessWrapper)
	{
		return AppendAnalyticsEventAttributeArray(Attributes,
			TEXT("ExeExitCode"), ProcessWrapper->ExitCode,
			TEXT("ExeRunTime"), ProcessWrapper->ExecutingTime,
			TEXT("ErrorCount"), ErrorListCache.Num()
			);
	}
	else
	{
		return Attributes;
	}	
}

bool FValidatorRunExecutable::DoesExecutableNeedBuilding() const
{
	const FValidatorRunExecutableDefinition* ExeDefinition = GetTypedDefinition<FValidatorRunExecutableDefinition>();
	check(ExeDefinition != nullptr);

	bool bExeNeedsBuilding = false;

	if (IFileManager::Get().FileExists(*ExeDefinition->ExecutablePath))
	{
		FBuildVersion VersionInfo;
		if (FindBuildVersionForExecutable(ExeDefinition->ExecutablePath, VersionInfo))
		{
			if (!VersionInfo.BuildUrl.IsEmpty())
			{
				UE_LOG(LogValidators, Log, TEXT("[%s] BuildVersion info for '%s' indicates that it is a precompiled binary"), *ValidatorName, *ExeDefinition->ExecutablePath);
				return false;
			}
			else
			{
				UE_LOG(LogValidators, Log, TEXT("[%s] BuildVersion info for '%s' indicates that it was built locally"), *ValidatorName, *ExeDefinition->ExecutablePath);
				return true;
			}
		}
		else
		{
			UE_LOG(LogValidators, Warning, TEXT("[%s] Failed to retrieve BuildVersion info for '%s', assuming that it was locally built"), *ValidatorName, *ExeDefinition->ExecutablePath);
			return true;
		}
	}
	else
	{
		UE_LOG(LogValidators, Log, TEXT("[%s] Failed to find '%s', so it will need to be built locally"), *ValidatorName, *ExeDefinition->ExecutablePath);
		return true;
	}
}

bool FValidatorRunExecutable::FindBuildVersionForExecutable(const FString& ExecutablePath, FBuildVersion& OutBuildVersion) const
{
	const FString VersionPath = FPathViews::ChangeExtension(ExecutablePath, TEXTVIEW("version"));
	if (IFileManager::Get().FileExists(*VersionPath))
	{
		FBuildVersion BuildVersion;
		if (FBuildVersion::TryRead(VersionPath, BuildVersion))
		{
			OutBuildVersion = MoveTemp(BuildVersion);
			return true;
		}
		else
		{
			return false;
		}
	}

	const FString TargetPath = FPathViews::ChangeExtension(ExecutablePath, TEXTVIEW("target"));
	if (IFileManager::Get().FileExists(*TargetPath))
	{
		FString JsonString;
		if (!FFileHelper::LoadFileToString(JsonString, *TargetPath))
		{
			return false;
		}

		TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(JsonString);
		TSharedPtr<FJsonObject> JsonRootObject;
		if (!FJsonSerializer::Deserialize(JsonReader, JsonRootObject))
		{
			return false;
		}

		const TSharedPtr<FJsonObject>* JsonVersionObject;
		if (!JsonRootObject->TryGetObjectField(TEXTVIEW("version"), JsonVersionObject))
		{
			return false;
		}

		FString JsonObjectString;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> JsonWriter = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&JsonObjectString, 0);
		if (!FJsonSerializer::Serialize((*JsonVersionObject).ToSharedRef(), JsonWriter, true))
		{
			return false;
		}

		FBuildVersion BuildVersion;
		if (FBuildVersion::TryReadFromString(JsonObjectString, BuildVersion))
		{
			OutBuildVersion = MoveTemp(BuildVersion);
			return true;
		}
		else
		{
			return false;
		}

	}

	return false;
}

void FValidatorRunExecutable::PrepareExecutableOptions()
{
	const FValidatorRunExecutableDefinition* TypedDefinition = GetTypedDefinition<FValidatorRunExecutableDefinition>();
	if(TypedDefinition->ExecutableCandidates.Num() > 0)
	{
		TMap<FString, FString> Options;
		Options.Reserve(TypedDefinition->ExecutableCandidates.Num());

		FValidatorRunExecutableDefinition* ModifyableDefinition = const_cast<FValidatorRunExecutableDefinition*>(TypedDefinition);

		FString SelectedOption;
		FString* UserSelectedOption = FSubmitToolUserPrefs::Get()->ValidatorOptions.Find(OptionsProvider.GetUserPrefsKey(ExecutableOptions));
		if(UserSelectedOption != nullptr && !UserSelectedOption->StartsWith(TEXT("Auto Select"), ESearchCase::IgnoreCase))
		{
			SelectedOption = *UserSelectedOption;
		}	

		FDateTime LastWriteAccess = FDateTime::MinValue();
		FFileManagerGeneric FileManager;

		FString NewestExecutable;	

		for(TPair<FString, FString>& ExecutableCandidate : ModifyableDefinition->ExecutableCandidates)
		{
			ExecutableCandidate.Value = FConfiguration::SubstituteAndNormalizeFilename(ExecutableCandidate.Value);
			Options.Add(ExecutableCandidate.Key, ExecutableCandidate.Value);

			if(FPaths::FileExists(*ExecutableCandidate.Value))
			{
				if(TypedDefinition->bUseLatestExecutable)
				{
					FFileStatData FileModifiedDate = FileManager.GetStatData(*ExecutableCandidate.Value);
					if(FileModifiedDate.ModificationTime > LastWriteAccess)
					{
						NewestExecutable = ExecutableCandidate.Key;
						LastWriteAccess = FileModifiedDate.ModificationTime;
					}
				}
				else if(SelectedOption.IsEmpty())
				{
					SelectedOption = ExecutableCandidate.Key;
				}
			}
		}

		if(TypedDefinition->bUseLatestExecutable && TypedDefinition->ExecutableCandidates.Contains(NewestExecutable))
		{
			const FString NewestExecutableOptionKey = FString::Printf(TEXT("Auto Select (%s)"), *NewestExecutable);
			Options.Add(NewestExecutableOptionKey, TypedDefinition->ExecutableCandidates[NewestExecutable]);

			if(UserSelectedOption == nullptr || UserSelectedOption->StartsWith(TEXT("Auto Select"), ESearchCase::IgnoreCase))
			{
				SelectedOption = NewestExecutableOptionKey;
			}
		}

		OptionsProvider.InitializeValidatorOptions(ExecutableOptions, Options, SelectedOption, EValidatorOptionType::FilePath);
	}
}
