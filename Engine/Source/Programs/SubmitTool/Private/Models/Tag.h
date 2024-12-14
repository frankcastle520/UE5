// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TagDefinition.h"

class FTag;

DECLARE_MULTICAST_DELEGATE_OneParam(FTagUpdated, const FTag&)

enum class ETagState : uint8
{
	Unchecked = 0,
	Failed,
	Success
};

class FTag {
public:
	FTag(const FTagDefinition& def) : FTag(def, std::numeric_limits<size_t>::max())
	{}

	FTag(const FTagDefinition& def, size_t start);

	FTag(const FTag& InOther) : FTag(InOther.Definition, InOther.StartPos)
	{}

	FString GetFullTag() const;
	bool ParseTag(const FString& source);
	
	void SetValues(const FString& valuesText);
	FString GetValuesText() const;

	void SetValues(const TArray<FString>& InValues); 
	const TArray<FString> GetValues(bool bIncludeInactive = false) const;

	const FTagValidationConfig& GetCurrentValidationConfig(const TArray<FString>& InDepotPaths) const;

	const ETagState GetTagState() const
	{
		return ValidationState;
	}

	void SetTagState(ETagState InState) const
	{
		ValidationState = InState;
	}

	bool IsEnabled() const
	{
		return StartPos != std::numeric_limits<size_t>::max();
	}

	FTagUpdated OnTagUpdated;

	bool bIsDirty = false;
	const FTagDefinition& Definition;
	size_t StartPos = std::numeric_limits<size_t>::max();
	size_t LastSize = std::numeric_limits<size_t>::max();
private:
	void Reset()
	{
		ValidationState = ETagState::Unchecked;
		bIsDirty = false;
		StartPos = std::numeric_limits<size_t>::max();
		LastSize = std::numeric_limits<size_t>::max();
		TagValues.Empty();
		OnTagUpdated.Broadcast(*this);
	}

	TArray<TCHAR const*> Delimiters;
	TArray<FString> SplittedDelims;
	TArray<FString> TagValues;
	mutable ETagState ValidationState;
};
