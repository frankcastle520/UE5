// Copyright Epic Games, Inc. All Rights Reserved.


#include "Tag.h"
#include "Misc/StringBuilder.h"
#include "Logging/SubmitToolLog.h"
#include "Internationalization/Regex.h"


FTag::FTag(const FTagDefinition& def, size_t start) : Definition(def), StartPos(start)
{
	for(const TCHAR& c : Definition.ValueDelimiter)
	{
		int idx = SplittedDelims.Add(FString::Chr(c));
		Delimiters.Add(*SplittedDelims[idx]);
	}
}

FString FTag::GetFullTag() const
{
	TStringBuilder<256> strBuilder;

	strBuilder.AppendChar('\n');
	strBuilder.Append(Definition.TagId);

	if(TagValues.Num() > 0)
	{
		strBuilder.AppendChar(' ');

		for(size_t i = 0; i < TagValues.Num(); ++i)
		{
			if(i != 0)
			{
				strBuilder.Append(Definition.ValueDelimiter);
			}
			strBuilder.Append(TagValues[i]);
		}
	}

	return strBuilder.ToString();
}

bool FTag::ParseTag(const FString& source)
{
	// regex pattern example, replacing tag, delimiter and min/maxvalues
	// (?:(?:\r\n|\r|\n)?TAGID)( +(?:[DELIMITERS]*(?!#)(?:[\w!"\$-\/\:-\@\[-\`\{-\~]+)){MINVALUES,MAXVALUES})?
	// (?:(?:\r\n|\r|\n)?#jira(?!\w))( +(?:[, ]*(?!#)(?:[\w!"\$-\/\:-\@\[-\`\{-\~]+)){1,256})?

	FString regexPat = "(?:(?:\\r\\n|\\r|\\n)?" + Definition.GetTagId() + "(?!\\w))( +(?:[" + Definition.ValueDelimiter + "]*(?!#)(?:[\\w!\"\\$-\\/\\:-\\@\\[-\\`\\{-\\~]+)){" + FString::FromInt(Definition.MinValues) + "," + FString::FromInt(Definition.MaxValues) + "})?";
	FRegexPattern Pattern = FRegexPattern(regexPat, ERegexPatternFlags::CaseInsensitive);
	FRegexMatcher regex = FRegexMatcher(Pattern, source);
	bool match = regex.FindNext();
	if(match)
	{
		bIsDirty = false;
		StartPos = regex.GetMatchBeginning();
		LastSize = regex.GetMatchEnding() - regex.GetMatchBeginning();

		UE_LOG(LogSubmitToolDebug, Log, TEXT("Start: %d"), regex.GetMatchBeginning());
		UE_LOG(LogSubmitToolDebug, Log, TEXT("Regex matched: %s"), *regex.GetCaptureGroup(0));

		FString capture = regex.GetCaptureGroup(1).TrimStart();
		capture.ParseIntoArray(TagValues, Delimiters.GetData(), SplittedDelims.Num());

		for(const FString& value : TagValues)
		{
			UE_LOG(LogSubmitToolDebug, Log, TEXT("Captured Value: %s"), *value);
		}

		UE_LOG(LogSubmitToolDebug, Log, TEXT("End: %d"), regex.GetMatchEnding());
	}
	else
	{
		Reset();
		UE_LOG(LogSubmitToolDebug, Log, TEXT("Tag %s not found in description"), *Definition.GetTagId());
	}

	if(OnTagUpdated.IsBound())
	{
		OnTagUpdated.Broadcast(*this);
	}
	return match;
}

void FTag::SetValues(const FString& valuesText)
{
	bIsDirty = true;
	TagValues.Empty();
	valuesText.ParseIntoArray(TagValues, Delimiters.GetData(), SplittedDelims.Num());
	ValidationState = ETagState::Unchecked;

	if(OnTagUpdated.IsBound())
	{
		OnTagUpdated.Broadcast(*this);
	}
}

FString FTag::GetValuesText() const
{
	TStringBuilder<256> strBuilder;
	for(size_t i = 0; i < TagValues.Num(); ++i)
	{
		if(i != 0)
		{
			strBuilder.Append(Definition.ValueDelimiter);
		}
		strBuilder.Append(TagValues[i]);
	}

	return strBuilder.ToString();
}

void FTag::SetValues(const TArray<FString>& InValues)
{
	bIsDirty = true;
	this->TagValues = InValues;

	for(size_t i = 0; i<TagValues.Num();++i)
	{
		bool removedChar;
		do
		{
			removedChar = false;
			for(const FString& Delim : SplittedDelims)
			{
				bool localRemoved;
				do
				{
					TagValues[i].TrimCharInline(Delim[0], &localRemoved);
					removedChar |= localRemoved;
				} while(localRemoved);
			}
		} while(removedChar);
	}

	if(OnTagUpdated.IsBound())
	{
		OnTagUpdated.Broadcast(*this);
	}
}

const TArray<FString> FTag::GetValues(bool bEvenIfDisabled) const
{
	if(IsEnabled() || bEvenIfDisabled)
	{
		return TagValues;
	}
	else
	{
		return TArray<FString>();
	}
}

const FTagValidationConfig& FTag::GetCurrentValidationConfig(const TArray<FString>& InDepotPaths) const
{
	if(Definition.ValidationOverrides.Num() != 0)
	{
		for(const FTagValidationOverride& ValidationOverride : Definition.ValidationOverrides)
		{
			FRegexPattern RegexPattern = FRegexPattern(ValidationOverride.RegexPath, ERegexPatternFlags::CaseInsensitive);
			for(const FString& Path : InDepotPaths)
			{
				FRegexMatcher Regex = FRegexMatcher(RegexPattern, Path);
				if(Regex.FindNext())
				{
					return ValidationOverride.ConfigOverride;
				}
			}
		}
	}

	return Definition.Validation;
}
