// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


#include "CoreMinimal.h"

#include "Logging/SubmitToolLog.h"
#include "Logic/ProcessWrapper.h"
#include "Parameters/SubmitToolParameters.h" 

#include "Logic/Services/SubmitToolServiceProvider.h"
#include "ISourceControlState.h"
#include "ValidatorDefinition.h"
#include "ValidatorOptionsProvider.h"

#include "ValidatorBase.generated.h"

class FTag;
struct FAnalyticsEventAttribute;

namespace SubmitToolParseConstants
{
	const FString BranchStateValidator = TEXT("BranchStateValidator");
	const FString ContentValidator = TEXT("ContentValidator");
	const FString TagValidator = TEXT("TagValidator");
	const FString UBTValidator = TEXT("UBTValidator");
	const FString CustomValidator = TEXT("CustomValidator");
	const FString CrossChangelistValidator = TEXT("CrossChangelistValidator");
	const FString PreflightValidator = TEXT("PreflightValidator");
	const FString InvalidateNode = TEXT("Invalidated");
	const FString VirtualizationToolOp = TEXT("VirtualizationTool");
};

UENUM()
enum class EValidationStates : uint8
{
	Not_Run,
	Skipped,
	Running,
	Valid,
	Failed,
	Timeout,
	Queued
};

template<class T>
concept DerivedFromDefinition = std::is_base_of<FValidatorDefinition, T>::value;

class FValidatorBase;
DECLARE_MULTICAST_DELEGATE_OneParam(FOnValidatorFinished, const FValidatorBase& /*Validator*/)

/**
 * @brief Base class for validator.
 */
class FValidatorBase
{
public:
	FValidatorBase() = delete;
	FValidatorBase(const FName& InNameId, const FSubmitToolParameters& InParameters, const TWeakPtr<FSubmitToolServiceProvider> InServiceProvider, const FString& InDefinition);
	virtual ~FValidatorBase();

	virtual void ParseDefinition(const FString& InDefinition);

	virtual const FString& GetValidatorTypeName() const = 0;
	const FName& GetValidatorNameId() const
	{
		return ValidatorNameID;
	}
	const FString& GetValidatorName() const
	{
		return ValidatorName;
	}

	void StartValidation();

	void Invalidate(bool bForce = false)
	{
		if(State == EValidationStates::Running)
		{
			CancelValidation();
		}
		else if(State != EValidationStates::Queued || bForce)
		{
			State = EValidationStates::Not_Run;
		}
	}

	bool GetHasPassed() const
	{
		return State == EValidationStates::Valid || State == EValidationStates::Skipped;
	}

	bool GetIsRunningOrQueued() const
	{
		return GetIsRunning() || GetIsQueued();
	}

	bool GetIsRunning() const
	{
		return State == EValidationStates::Running;
	}

	bool GetIsQueued() const
	{
		return State == EValidationStates::Queued;
	}

	float GetRunTime() const
	{
		return RunTime;
	}

	const EValidationStates& GetState()
	{
		return State;
	}

	virtual void Tick(float InDeltaTime);

	virtual void CancelValidation()
	{
		if(State == EValidationStates::Running)
		{
			StopInternalValidations();
		}

		State = EValidationStates::Not_Run;
	}

	virtual void SetQueued(bool InbForceRun = false)
	{
		bForceRun = InbForceRun;
		if(State == EValidationStates::Running)
		{
			StopInternalValidations();
		}

		State = EValidationStates::Queued;
	}

	virtual bool Activate();

	virtual void InvalidateLocalFileModifications();
	virtual bool EvaluateTagSkip();

	const FString GetStatusText() const;
	const EValidationStates GetValidatorState() const
	{
		return State;
	}

	const TMap<FString, TMap<FString, FString>>& GetValidatorOptions() const
	{
		return OptionsProvider.GetValidatorOptions();
	}

	const FString GetSelectedOptionValue(const FString& InOptionName) const
	{
		return OptionsProvider.GetSelectedOptionValue(InOptionName);
	}
	const FString GetSelectedOptionKey(const FString& InOptionName) const
	{
		return OptionsProvider.GetSelectedOptionKey(InOptionName);
	}
	EValidatorOptionType GetOptionType(const FString& InOptionName) const
	{
		return OptionsProvider.GetOptionType(InOptionName);
	}

	void SetSelectedOption(const FString& InOptionName, const FString& InOptionValue);

	virtual const TArray<FAnalyticsEventAttribute> GetTelemetryAttributes() const;
	virtual void PrintErrorSummary() const;

	const TArray<FString>& GetErrorList() const
	{
		return ErrorListCache;
	}

	const FString GetValidationConfigId() const;

	template<DerivedFromDefinition T>
	const T* GetTypedDefinition() const;

	TArray<FName> Dependants;
	TUniquePtr<const FValidatorDefinition> Definition;
	FOnValidatorFinished OnValidationFinished;

protected:
	FName ValidatorNameID;
	FString ValidatorName;
	FValidatorOptionsProvider OptionsProvider;
	TWeakPtr<FSubmitToolServiceProvider> ServiceProvider;
	TArray<FSourceControlStateRef> FilteredFiles;
	mutable TArray<FString> ErrorListCache;
	bool bIsValidSetup = false;
	bool bForceRun = false;
	const FSubmitToolParameters& SubmitToolParameters;

	FDateTime Start;
	float RunTime = 0;

	/**
	 * @brief Starts the validator.
	 * @param SubmitSettings The submit settings to use for this validation cycle.
	 * @return Returns true if validation has started successfully, false if validation failed.
	 */
	virtual bool Validate(const FString& CLDescription, const TArray<FSourceControlStateRef>& FilteredFilesInCL, const TArray<const FTag*>& Tags) = 0;

	virtual bool AppliesToFile(const FSourceControlStateRef InFile, bool InbAllowIncremental, bool& OutbIsIncrementalSkip) const;
	virtual bool AppliesToCL(const FString& CLDescription, const TArray<FSourceControlStateRef>& FilesInCL, const TArray<const FTag*>& Tags, TArray<FSourceControlStateRef>& OutFilteredFiles, TArray<FSourceControlStateRef>& OutIncrementalSkips, bool InbAllowIncremental) const;
	virtual void StopInternalValidations()
	{};
	virtual void ValidationFinished(const bool bHasPassed);

	virtual void LogFailure(const FString& FormattedMsg) const
	{
		UE_LOG(LogValidators, Error, TEXT("%s"), *FormattedMsg);
		UE_LOG(LogValidatorsResult, Error, TEXT("%s"), *FormattedMsg);

		ErrorListCache.Add(FormattedMsg);
	}

	virtual void Skip()
	{
		State = EValidationStates::Skipped; OnValidationFinished.Broadcast(*this);
	}
private:
	EValidationStates State = EValidationStates::Not_Run;
};

template<DerivedFromDefinition T>
inline const T* FValidatorBase::GetTypedDefinition() const
{
	return  static_cast<const T*>(Definition.Get());
}
