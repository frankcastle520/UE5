// Copyright Epic Games, Inc. All Rights Reserved.

#include "ValidatorBase.h"
#include "HAL/FileManagerGeneric.h"
#include "Misc/Paths.h"
#include "Misc/Timespan.h"
#include "AnalyticsEventAttribute.h"
#include "Configuration/Configuration.h"
#include "Logic/TagService.h"
#include "Logic/Services/Interfaces/ICacheDataService.h"
#include "Logic/Services/Interfaces/ISTSourceControlService.h"

FValidatorBase::FValidatorBase(const FName& InNameId, const FSubmitToolParameters& InParameters, TWeakPtr<FSubmitToolServiceProvider> InServiceProvider, const FString& InDefinition) :
	ValidatorNameID(InNameId),
	OptionsProvider(InNameId),
	ServiceProvider(InServiceProvider),
	SubmitToolParameters(InParameters),
	Start(FDateTime::MinValue())
{
	ParseDefinition(InDefinition);
	ValidatorName = Definition->CustomName.IsEmpty() ? ValidatorNameID.ToString() : Definition->CustomName;
}

void FValidatorBase::ParseDefinition(const FString& InDefinition)
{
	Definition = MakeUnique<FValidatorDefinition>();
	FStringOutputDevice Errors;
	FValidatorDefinition::StaticStruct()->ImportText(*InDefinition, const_cast<FValidatorDefinition*>(Definition.Get()), nullptr, 0, &Errors, FValidatorDefinition::StaticStruct()->GetName());

	if(!Errors.IsEmpty())
	{
		UE_LOG(LogSubmitTool, Error, TEXT("[%s] Error loading parameter file %s"), *GetValidatorNameId().ToString(), *Errors);
	}

}

FValidatorBase::~FValidatorBase()
{
	OnValidationFinished.Clear();
}

void FValidatorBase::StartValidation()
{
	RunTime = 0;
	Start = FDateTime::UtcNow();
	State = EValidationStates::Running;
	ErrorListCache.Empty();

	const TSharedPtr<FChangelistService>& ChangelistService = ServiceProvider.Pin()->GetService<FChangelistService>();
	const TSharedPtr<FTagService>& TagService = ServiceProvider.Pin()->GetService<FTagService>();

	FilteredFiles.RemoveAt(0, FilteredFiles.Num(), EAllowShrinking::No);

	bool bIncrementalValidation = Definition->bUsesIncrementalCache && !bForceRun;
	bForceRun = false;
	TArray<FSourceControlStateRef> IncrementallySkippedFiles;
	bool bAppliesToCL = AppliesToCL(ChangelistService->GetCLDescription(), ChangelistService->GetFilesInCL(), TagService->GetTagsArray(), FilteredFiles, IncrementallySkippedFiles, bIncrementalValidation);

	if(bAppliesToCL && !bIsValidSetup)
	{
		LogFailure(FString::Printf(TEXT("[%s] Task is not correctly setup and should run in this CL"), *ValidatorName));
		ValidationFinished(false);
	}
	else if(!bAppliesToCL)
	{
		if(IncrementallySkippedFiles.Num() != 0)
		{
			UE_LOG(LogValidators, Log, TEXT("[%s] All files were validated in a previous validation and are still valid. To force a validation click 'Run' in the validator list"), *ValidatorName );
			UE_LOG(LogValidatorsResult, Log, TEXT("[%s] All files were validated in a previous validation and are still valid. To force a validation click 'Run' in the validator list"), *ValidatorName);
		}
		else
		{
			const FString Extensions = Definition->IncludeFilesWithExtension.IsEmpty() ? TEXT(".*") : *FString::Join(Definition->IncludeFilesWithExtension, TEXT("|"));
			UE_LOG(LogValidators, Log, TEXT("[%s] No files match the filter %s{%s} %s won't run"), *ValidatorName, *Definition->IncludeFilesInDirectory, *Extensions, *ValidatorName);
			UE_LOG(LogValidatorsResult, Log, TEXT("[%s] No files match the filter %s{%s} %s won't run"), *ValidatorName, *Definition->IncludeFilesInDirectory, *Extensions, *ValidatorName);
		}
		Skip();
	}
	else
	{
		if(IncrementallySkippedFiles.Num() != 0)
		{
			const FString FileList = FString::JoinBy(IncrementallySkippedFiles, TEXT("\n"), [](const FSourceControlStateRef& InFile) { return InFile->GetFilename(); });
			UE_LOG(LogValidators, Log, TEXT("[%s] Skipping Files because they were already validated in a previous execution:\n%s"), *GetValidatorName(), *FileList);
		}

		if(!Validate(ChangelistService->GetCLDescription(), FilteredFiles, TagService->GetTagsArray()))
		{
			ValidationFinished(false);
		}
	}
}


void FValidatorBase::Tick(float InDeltaTime)
{
	RunTime += InDeltaTime;

	if(Definition->TimeoutLimit > 0 && RunTime >= Definition->TimeoutLimit)
	{
		LogFailure(FString::Printf(TEXT("[%s]: %s"), *GetValidatorName(), TEXT("Timeout limit has been reached, cancelling task.")));

		StopInternalValidations();
		State = EValidationStates::Timeout;

		if(OnValidationFinished.IsBound())
		{
			OnValidationFinished.Broadcast(*this);
		}
	}
}

bool FValidatorBase::Activate()
{
	bIsValidSetup = true;

	if(Definition != nullptr)
	{
		if(!Definition->IncludeFilesInDirectory.IsEmpty())
		{
			FValidatorDefinition* ModifiableDefinition = const_cast<FValidatorDefinition*>(Definition.Get());
			ModifiableDefinition->IncludeFilesInDirectory = FConfiguration::SubstituteAndNormalizeDirectory(ModifiableDefinition->IncludeFilesInDirectory);
		}
	}
	else
	{
		bIsValidSetup = false;
	}

	return bIsValidSetup;
}

void FValidatorBase::InvalidateLocalFileModifications()
{
	if((Definition->TaskArea & ETaskArea::LocalFiles) == ETaskArea::LocalFiles && (State == EValidationStates::Valid || State == EValidationStates::Running || State == EValidationStates::Skipped))
	{
		FFileManagerGeneric FileManager;
		for(const FSourceControlStateRef& File : ServiceProvider.Pin()->GetService<FChangelistService>()->GetFilesInCL())
		{
			bool bIncrementallySkipped;

			if(AppliesToFile(File, false, bIncrementallySkipped))
			{
				FString Filename = File->GetFilename();
				FFileStatData FileModifiedDate = FileManager.GetStatData(*Filename);
				if(FileModifiedDate.ModificationTime > Start)
				{
					if(GetIsRunning())
					{
						UE_LOG(LogValidators, Warning, TEXT("File %s was modified during %s run, this task needs to be run again"), *Filename, *GetValidatorName());
						UE_LOG(LogValidatorsResult, Warning, TEXT("File %s was modified during %s run, this task needs to be run again"), *Filename, *GetValidatorName());
					}
					else
					{
						UE_LOG(LogValidators, Warning, TEXT("File %s has been modified after %s last run, this task needs to be run again."), *Filename, *GetValidatorName());
						UE_LOG(LogValidatorsResult, Warning, TEXT("File %s has been modified after %s last run, this task needs to be run again."), *Filename, *GetValidatorName());
					}

					Invalidate();
					break;
				}
			}
		}
	}
}

const FString FValidatorBase::GetStatusText() const
{
	const FString StateStr = StaticEnum<EValidationStates>()->GetNameStringByValue(static_cast<int64>(State))
		.Replace(TEXT("_"), TEXT(" "));

	if(State == EValidationStates::Skipped || State == EValidationStates::Not_Run)
	{
		return StateStr;
	}

	// do not clutter the UI with uninteresting information
	if (RunTime < 0.5f)
	{
		return StateStr;
	}

	return FString::Printf(TEXT("%s (%s)"),
		*StateStr,
		*FGenericPlatformTime::PrettyTime(RunTime));
}

const TArray<FAnalyticsEventAttribute> FValidatorBase::GetTelemetryAttributes() const
{
	return MakeAnalyticsEventAttributeArray(
		TEXT("ValidatorID"), *GetValidatorNameId().ToString(),
		TEXT("ValidatorName"), *GetValidatorName(),
		TEXT("Status"), GetHasPassed(),
		TEXT("Runtime"), RunTime,
		TEXT("Stream"), ServiceProvider.Pin()->GetService<ISTSourceControlService>()->GetCurrentStreamName()
		);
}

void FValidatorBase::ValidationFinished(const bool bHasPassed)
{
	if(bHasPassed)
	{
		UE_LOG(LogValidatorsResult, Log, TEXT("[%s]: Task Succeeded!"), *GetValidatorName());

		if(Definition->bUsesIncrementalCache)
		{
			ServiceProvider.Pin()->GetService<ICacheDataService>()->UpdateLastValidationForFiles(ServiceProvider.Pin()->GetService<FChangelistService>()->GetCLID(), GetValidatorNameId(), GetValidationConfigId(), FilteredFiles, FDateTime::UtcNow());
		}
	}
	else if(Definition->IsRequired)
	{
		UE_LOG(LogValidatorsResult, Error, TEXT("[%s]: Failed on Required Task!"), *GetValidatorName());
	}
	else
	{
		UE_LOG(LogValidatorsResult, Warning, TEXT("[%s]: Failed on Optional Task!"), *GetValidatorName());
	}

	if(!bHasPassed)
	{
		for(const FString& ErrorMsg : Definition->AdditionalValidationErrorMessages)
		{
			LogFailure(FString::Printf(TEXT("[%s]: %s"), *GetValidatorName(), *ErrorMsg));
		}
	}

	State = bHasPassed ? EValidationStates::Valid : EValidationStates::Failed;

	if(OnValidationFinished.IsBound())
	{
		OnValidationFinished.Broadcast(*this);
	}
}

bool FValidatorBase::EvaluateTagSkip()
{
	TSharedPtr<FChangelistService> ChangelistService = ServiceProvider.Pin()->GetService<FChangelistService>();
	if(Definition->SkipForbiddenTags.Num() > 0)
	{
		for(const FString& Tag : Definition->SkipForbiddenTags)
		{
			if(ChangelistService->GetCLDescription().Find(Tag, ESearchCase::IgnoreCase) != INDEX_NONE)
			{
				UE_LOG(LogValidators, Log, TEXT("[%s] The Description contains '%s'. %s is not allowed to be skipped"), *ValidatorName, *Tag, *ValidatorName);
				UE_LOG(LogValidatorsResult, Log, TEXT("[%s] The Description contains '%s'. %s is not allowed to be skipped"), *ValidatorName, *Tag, *ValidatorName);
				return false;
			}
		}
	}

	if(Definition->bSkipWhenAddendumInDescription && !Definition->ChangelistDescriptionAddendum.IsEmpty())
	{
		if(ChangelistService->GetCLDescription().Find(Definition->ChangelistDescriptionAddendum, ESearchCase::IgnoreCase) != INDEX_NONE)
		{
			UE_LOG(LogValidators, Log, TEXT("[%s] The Description Addendum '%s' is already present in the CL. %s won't run"), *ValidatorName, *Definition->ChangelistDescriptionAddendum, *ValidatorName);
			UE_LOG(LogValidatorsResult, Log, TEXT("[%s] The Description Addendum '%s' is already present in the CL. %s won't run"), *ValidatorName, *Definition->ChangelistDescriptionAddendum, *ValidatorName);
			Start = FDateTime::UtcNow();
			State = EValidationStates::Skipped;
			return true;
		}
	}

	return false;
}

void FValidatorBase::SetSelectedOption(const FString& InOptionName, const FString& InOptionValue)
{
	UE_LOG(LogValidators, Log, TEXT("[%s] Task stopped due to a change in options, %s = %s"), *GetValidatorName(), *InOptionName, *InOptionValue);
	CancelValidation();
	OptionsProvider.SetSelectedOption(InOptionName, InOptionValue);
}

void FValidatorBase::PrintErrorSummary() const
{
	if(State == EValidationStates::Failed || State == EValidationStates::Timeout)
	{
		if(ErrorListCache.Num() > 0)
		{
			UE_LOG(LogValidators, Error, TEXT("========================[%s Errors Summary]========================"), *GetValidatorName());
			UE_LOG(LogValidatorsResult, Error, TEXT("========================[%s Errors Summary]========================"), *GetValidatorName());
			for(const FString& ErrorStr : ErrorListCache)
			{
				UE_LOG(LogValidators, Error, TEXT("%s"), *ErrorStr);
				UE_LOG(LogValidatorsResult, Error, TEXT("%s"), *ErrorStr);
			}
			UE_LOG(LogValidators, Error, TEXT("================================================================"), *GetValidatorName());
			UE_LOG(LogValidatorsResult, Error, TEXT("================================================================"), *GetValidatorName());
		}
	}
}
const FString FValidatorBase::GetValidationConfigId() const
{
	TStringBuilder<512> StringBuilder;
	for(const TPair<FString, FString>& Pair : OptionsProvider.GetSelectedOptions())
	{
		StringBuilder.Append(Pair.Key);
		StringBuilder.Append(TEXT("_"));
		StringBuilder.Append(Pair.Value);
		StringBuilder.Append(TEXT("-"));
	}

	return StringBuilder.ToString();
}

bool FValidatorBase::AppliesToFile(const FSourceControlStateRef InFile, bool InbAllowIncremental, bool& OutbIsIncrementalSkip) const
{
	bool bIncluded = false;
	OutbIsIncrementalSkip = false;

	if((Definition->TaskArea & ETaskArea::LocalFiles) == ETaskArea::None)
	{
		// For validators that do not work on local files, we always apply
		return true;
	}
	

	if(!InFile->IsDeleted()
		|| (InFile->IsDeleted() && Definition->bAcceptDeletedFiles))
	{
		FString Filename = InFile->GetFilename();
		FPaths::NormalizeFilename(Filename);

		if(!Definition->IncludeFilesInDirectory.IsEmpty())
		{
			if(!Filename.StartsWith(Definition->IncludeFilesInDirectory, ESearchCase::IgnoreCase))
			{
				return false;
			}
		}

		if(Definition->IncludeFilesWithExtension.IsEmpty())
		{
			bIncluded = true;
		}

		for(int Idx = 0; Idx < Definition->IncludeFilesWithExtension.Num(); Idx++)
		{
			if(Filename.EndsWith(Definition->IncludeFilesWithExtension[Idx], ESearchCase::IgnoreCase))
			{
				bIncluded = true;
				break;
			}
		}

		if(InbAllowIncremental)
		{
			FFileManagerGeneric FileManager;
			FDateTime LastValidation = ServiceProvider.Pin()->GetService<ICacheDataService>()->GetLastValidationDate(ServiceProvider.Pin()->GetService<FChangelistService>()->GetCLID(), GetValidatorNameId(), GetValidationConfigId(), InFile->GetFilename());
			FFileStatData FileModifiedDate = FileManager.GetStatData(*Filename);
			if(LastValidation != FDateTime::MinValue() && FileModifiedDate.ModificationTime < LastValidation)
			{
				OutbIsIncrementalSkip = true;
				bIncluded = false;
			}
		}

	}

	return bIncluded;
}


bool FValidatorBase::AppliesToCL(const FString& InCLDescription, const TArray<FSourceControlStateRef>& FilesInCL, const TArray<const FTag*>& Tags, TArray<FSourceControlStateRef>& OutFilteredFiles, TArray<FSourceControlStateRef>& OutIncrementalSkips, bool InbAllowIncremental) const
{
	for(const FSourceControlStateRef& File : FilesInCL)
	{
		bool bIsIncrementalSkip;
		if(AppliesToFile(File, InbAllowIncremental, bIsIncrementalSkip))
		{
			OutFilteredFiles.Add(File);
		}
		else if(bIsIncrementalSkip)
		{
			OutIncrementalSkips.Add(File);
		}
	}

	return OutFilteredFiles.Num() > 0;
}
