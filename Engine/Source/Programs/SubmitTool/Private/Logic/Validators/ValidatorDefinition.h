// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Class.h"

#include "ValidatorDefinition.generated.h"

UENUM()
enum class ETaskArea : uint8
{
	None =			0,
	Changelist =	1 << 0,
	LocalFiles =	1 << 1,
	ShelvedFiles =	1 << 2,


	ShelveAndLocalFiles = LocalFiles | ShelvedFiles,
	Everything =	UINT8_MAX

};
ENUM_CLASS_FLAGS(ETaskArea)

USTRUCT()
struct FValidatorDefinition
{
	GENERATED_BODY()

	///
	/// Type of the validator, this is restricted to the classes that are implemented and derive from FValidatorBase
	/// examples include CustomValidator, TagValidator, UBTValidator and others
	/// @see SubmitToolParseConstants
	/// @see FValidatorBase
	///
	UPROPERTY()
	FString Type;
	
	/// 
	/// Whether the validator is required to allow submission or not, a failing required validation will always block submission
	/// 
	UPROPERTY()
	bool IsRequired = true;
	
	/// 
	/// Whether the validator is required to finish running before allowing submission
	/// 
	UPROPERTY()
	bool bRequireCompleteWhenOptional = false;

	///
	/// Maximum time that a validator will run before being cancelled out
	/// 
	UPROPERTY()
	float TimeoutLimit = -1;
	
	///
	/// Name of this instance of the validator that will be used for display
	/// 
	UPROPERTY()
	FString CustomName;

	///
	/// Files with any of these extensions will be included
	/// 
	UPROPERTY()
	TArray<FString> IncludeFilesWithExtension;

	///
	///	Only run this validator for files under this directory
	/// 
	UPROPERTY()
	FString IncludeFilesInDirectory;

	///
	///	This text will be added to the description if this validation passes
	/// 
	UPROPERTY()
	FString ChangelistDescriptionAddendum;

	///
	///	Skip this validator when the addendum is already present in the CL description
	/// 
	UPROPERTY()
	bool bSkipWhenAddendumInDescription;

	///
	///	Skip is forbidden when this text if found in the CL description
	/// 
	UPROPERTY()
	TArray<FString> SkipForbiddenTags;

	///
	///	Skip is forbidden when this text if found in the CL description
	/// 
	UPROPERTY()
	FString ConfigFilePath;	

	///
	///	List of Validator Ids that needs to succeed before this validator runs
	/// 
	UPROPERTY()
	TArray<FName> DependsOn;

	///
	///	List of execution groups this Validator is part of. Two validators with an execution group in commmon cannot run concurrently
	/// 
	UPROPERTY()
	TArray<FName> ExecutionBlockGroups;
	
	///
	///	Name of the UI Section this Validator is part of
	/// 
	UPROPERTY()
	FName UIGroup;

	///
	///	If this Validator runs on files marked for delete
	/// 
	UPROPERTY()
	bool bAcceptDeletedFiles = false;

	///
	///	If this Validator should treat warnings as errors
	/// 
	UPROPERTY()
	bool bTreatWarningsAsErrors = false;
	
	UPROPERTY()
	bool bInvalidatesWhenOutOfDate = false;

	///
	///	If this validator maintains a local cache of results per file between runs on the same CL, used for incremental validations
	/// 
	UPROPERTY()
	bool bUsesIncrementalCache = false;

	///
	///	Additional error messages to print when this validation fails
	/// 
	UPROPERTY()
	TArray<FString> AdditionalValidationErrorMessages;

	///
	///	Tooltip when hovering over the Validator
	/// 
	UPROPERTY()
	FString ToolTip;

	///
	///	Area this validator works on, if an area is updated, the validator state will be automatically resetted { Everything, LocalFiles, ShelvedFiles, LocalAndShelvedFiles, Changelist } 
	/// 
	UPROPERTY()
	ETaskArea TaskArea = ETaskArea::Everything;
};

USTRUCT()
struct FValidatorRunExecutableDefinition : public FValidatorDefinition
{
	GENERATED_BODY()

	UPROPERTY()
	bool bLaunchHidden = true;
	
	UPROPERTY()
	bool bLaunchReallyHidden = true;

	///
	///	Path to the executable that this validator runs
	/// 
	UPROPERTY()
	FString ExecutablePath;

	///
	///	Possible Executable paths for this validator to use (user selects)
	/// 
	UPROPERTY()
	TMap<FString, FString> ExecutableCandidates;

	///
	///	When using ExecutableCandidates, default select the newest one
	/// 
	UPROPERTY()
	bool bUseLatestExecutable;

	///
	///	Arguments to pass to the Executable
	/// 
	UPROPERTY()
	FString ExecutableArguments;

	UPROPERTY()
	FString FileInCLArgument;

	///
	///	If specified, list of files will written into a text file and appended to this i.e: a value of "Filelist=" will be 
	/// processed to be "Filelist=Path/To/Intermediate/SubmitTool/FileLists/GUID.txt"
	/// 
	UPROPERTY()
	FString FileListArgument;

	///
	///	When parsing process output, treat these messages as errors
	/// 
	UPROPERTY()
	TArray<FString> ErrorMessages;

	///
	///	When parsing process output, ignore these error messages
	/// 
	UPROPERTY()
	TArray<FString> IgnoredErrorMessages;

	///
	///	When evaluating process exit code, treat these list as a success (defaults to 0)
	/// 
	UPROPERTY()
	TArray<int32> AllowedExitCodes = {0};

	///
	///	Only evaluate validator success using exit code of a process, ignore any output parsing
	/// 
	UPROPERTY()
	bool bOnlyLookAtExitCode = false;

	///
	///	if present, when parsing process output, from this message on, ignore the output
	/// 
	UPROPERTY()
	FString DisableOutputErrorsAnchor;

	///
	///	if Present, when parsing process output, from this message on, parse the output
	/// 
	UPROPERTY()
	FString EnableOutputErrorsAnchor;

	///
	///	Regex for identifying errors from the output of a process
	/// 
	UPROPERTY()
	FString RegexErrorParsing = TEXT("^(?!.*(?:Display: |Warning: |Log: )).*( error |error:).*$");

	///
	///	Regex for identifying warnings from the output of a process
	/// 
	UPROPERTY()
	FString RegexWarningParsing = TEXT("^(?!.*(?:Display: |Log: ).*( warning |warning:).*$");
};


USTRUCT()
struct FUBTValidatorDefinition : public FValidatorRunExecutableDefinition
{
	GENERATED_BODY()

	UPROPERTY()
	FString Configuration;

	UPROPERTY()
	FString Platform;

	UPROPERTY()
	FString Target;

	UPROPERTY()
	FString ProjectArgument;

	UPROPERTY()
	FString TargetListArgument;

	UPROPERTY()
	TArray<FString> Configurations;

	UPROPERTY()
	TArray<FString> Platforms;

	UPROPERTY()
	TArray<FString> Targets;

	UPROPERTY()
	TArray<FString> StaticAnalysers;

	UPROPERTY()
	FString StaticAnalyserArg;

	UPROPERTY()
	FString StaticAnalyser;

	UPROPERTY()
	bool bUseStaticAnalyser = false;
};

USTRUCT()
struct FVirtualizationToolDefinition : public FValidatorRunExecutableDefinition
{
	GENERATED_BODY()

	UPROPERTY()
	bool bIncludePackages;

	UPROPERTY()
	bool bIncludeTextPackages;

	UPROPERTY()
	FString BuildCommand;

	UPROPERTY()
	FString BuildCommandArgs;
};
