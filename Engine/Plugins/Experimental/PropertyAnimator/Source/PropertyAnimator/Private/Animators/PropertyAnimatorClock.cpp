// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animators/PropertyAnimatorClock.h"

#include "Misc/DateTime.h"
#include "Properties/Converters/PropertyAnimatorCoreConverterBase.h"
#include "Properties/Handlers/PropertyAnimatorCoreHandlerBase.h"
#include "Subsystems/PropertyAnimatorCoreSubsystem.h"

namespace UE::Private::Animator
{
	static TMap<TCHAR, TFunction<FString(const FDateTime&)>> FormatFunctions;
}

void UPropertyAnimatorClock::RegisterFormat(const TCHAR InChar, TFunction<FString(const FDateTime&)> InFormatter)
{
	UE::Private::Animator::FormatFunctions.Add(InChar, InFormatter);
}

void UPropertyAnimatorClock::UnregisterFormat(const TCHAR InChar)
{
	UE::Private::Animator::FormatFunctions.Remove(InChar);
}

UPropertyAnimatorClock::UPropertyAnimatorClock()
{
	if (IsTemplate())
	{
		RegisterFormat(TEXT('a'), [](const FDateTime& InDateTime)->FString{ return InDateTime.ToFormattedString(TEXT("%a")); });
		RegisterFormat(TEXT('A'), [](const FDateTime& InDateTime)->FString{ return InDateTime.ToFormattedString(TEXT("%A")); });
		RegisterFormat(TEXT('w'), [](const FDateTime& InDateTime)->FString{ return InDateTime.ToFormattedString(TEXT("%w")); });
		RegisterFormat(TEXT('y'), [](const FDateTime& InDateTime)->FString{ return InDateTime.ToFormattedString(TEXT("%y")); });
		RegisterFormat(TEXT('Y'), [](const FDateTime& InDateTime)->FString{ return InDateTime.ToFormattedString(TEXT("%Y")); });
		RegisterFormat(TEXT('b'), [](const FDateTime& InDateTime)->FString{ return InDateTime.ToFormattedString(TEXT("%b")); });
		RegisterFormat(TEXT('B'), [](const FDateTime& InDateTime)->FString{ return InDateTime.ToFormattedString(TEXT("%B")); });
		RegisterFormat(TEXT('m'), [](const FDateTime& InDateTime)->FString{ return InDateTime.ToFormattedString(TEXT("%m")); });
		RegisterFormat(TEXT('n'), [](const FDateTime& InDateTime)->FString{ return FString::FromInt(InDateTime.GetMonth()); });
		RegisterFormat(TEXT('d'), [](const FDateTime& InDateTime)->FString{ return InDateTime.ToFormattedString(TEXT("%d")); });
		RegisterFormat(TEXT('e'), [](const FDateTime& InDateTime)->FString{ return InDateTime.ToFormattedString(TEXT("%e")); });
		RegisterFormat(TEXT('j'), [](const FDateTime& InDateTime)->FString{ return InDateTime.ToFormattedString(TEXT("%j")); });
		RegisterFormat(TEXT('J'), [](const FDateTime& InDateTime)->FString{ return FString::FromInt(InDateTime.GetDayOfYear()); });
		RegisterFormat(TEXT('l'), [](const FDateTime& InDateTime)->FString{ return InDateTime.ToFormattedString(TEXT("%l")); });
		RegisterFormat(TEXT('I'), [](const FDateTime& InDateTime)->FString{ return InDateTime.ToFormattedString(TEXT("%I")); });
		RegisterFormat(TEXT('H'), [](const FDateTime& InDateTime)->FString{ return InDateTime.ToFormattedString(TEXT("%H")); });
		RegisterFormat(TEXT('h'), [](const FDateTime& InDateTime)->FString{ return FString::FromInt(InDateTime.GetHour()); });
		RegisterFormat(TEXT('M'), [](const FDateTime& InDateTime)->FString{ return InDateTime.ToFormattedString(TEXT("%M")); });
		RegisterFormat(TEXT('N'), [](const FDateTime& InDateTime)->FString{ return FString::FromInt(InDateTime.GetMinute()); });
		RegisterFormat(TEXT('S'), [](const FDateTime& InDateTime)->FString{ return InDateTime.ToFormattedString(TEXT("%S")); });
		RegisterFormat(TEXT('s'), [](const FDateTime& InDateTime)->FString{ return FString::FromInt(InDateTime.GetSecond()); });
		RegisterFormat(TEXT('f'), [](const FDateTime& InDateTime)->FString{ return InDateTime.ToString(TEXT("%s")); });
		RegisterFormat(TEXT('F'), [](const FDateTime& InDateTime)->FString{ return FString::FromInt(InDateTime.GetMillisecond()); });
		RegisterFormat(TEXT('p'), [](const FDateTime& InDateTime)->FString{ return InDateTime.ToFormattedString(TEXT("%p")); });
		RegisterFormat(TEXT('P'), [](const FDateTime& InDateTime)->FString{ return InDateTime.ToFormattedString(TEXT("%P")); });
		RegisterFormat(TEXT('t'), [](const FDateTime& InDateTime)->FString{ return LexToString(InDateTime.GetTicks()); });
	}
}

void UPropertyAnimatorClock::SetDisplayFormat(const FString& InDisplayFormat)
{
	DisplayFormat = InDisplayFormat;
}

FString UPropertyAnimatorClock::FormatDateTime(const FDateTime& InDateTime, const FString& InDisplayFormat)
{
	FString Result;
	Result.Reserve(InDisplayFormat.Len());

    for (int32 CharIndex = 0; CharIndex < InDisplayFormat.Len(); ++CharIndex)
    {
        if (InDisplayFormat[CharIndex] == TEXT('%') && CharIndex + 1 < InDisplayFormat.Len())
        {
			if (const TFunction<FString(const FDateTime&)>* Formatter = UE::Private::Animator::FormatFunctions.Find(InDisplayFormat[CharIndex + 1]))
			{
				Result += (*Formatter)(InDateTime);
			}

            ++CharIndex;
        }
        else
        {
            Result += InDisplayFormat[CharIndex];
        }
    }

    return Result;
}

void UPropertyAnimatorClock::OnAnimatorRegistered(FPropertyAnimatorCoreMetadata& InMetadata)
{
	Super::OnAnimatorRegistered(InMetadata);

	InMetadata.Name = TEXT("Clock");
}

void UPropertyAnimatorClock::EvaluateProperties(FInstancedPropertyBag& InParameters)
{
	const double TimeElapsed = InParameters.GetValueDouble(TimeElapsedParameterName).GetValue();

	const FTimespan ElapsedTimeSpan = FTimespan::FromSeconds(TimeElapsed);
	const FDateTime DateTime(ElapsedTimeSpan > FTimespan::Zero() ? ElapsedTimeSpan.GetTicks() : 0);
	const FString FormattedDateTime = FormatDateTime(DateTime, DisplayFormat);

	EvaluateEachLinkedProperty([this, FormattedDateTime](
		UPropertyAnimatorCoreContext* InContext
		, const FPropertyAnimatorCoreData& InResolvedProperty
		, FInstancedPropertyBag& InEvaluatedValues
		, int32 InRangeIndex
		, int32 InRangeMax)->bool
	{
		const FName DisplayName(InResolvedProperty.GetPathHash());

		InEvaluatedValues.AddProperty(DisplayName, EPropertyBagPropertyType::String);
		InEvaluatedValues.SetValueString(DisplayName, FormattedDateTime);

		return true;
	});
}

bool UPropertyAnimatorClock::ImportPreset(const UPropertyAnimatorCorePresetBase* InPreset, const TSharedRef<FPropertyAnimatorCorePresetArchive>& InValue)
{
	if (Super::ImportPreset(InPreset, InValue) && InValue->IsObject())
	{
		const TSharedPtr<FPropertyAnimatorCorePresetObjectArchive> AnimatorArchive = InValue->AsMutableObject();

		FString DisplayFormatValue = DisplayFormat;
		AnimatorArchive->Get(GET_MEMBER_NAME_STRING_CHECKED(UPropertyAnimatorClock, DisplayFormat), DisplayFormatValue);
		SetDisplayFormat(DisplayFormatValue);

		return true;
	}

	return false;
}

bool UPropertyAnimatorClock::ExportPreset(const UPropertyAnimatorCorePresetBase* InPreset, TSharedPtr<FPropertyAnimatorCorePresetArchive>& OutValue) const
{
	if (Super::ExportPreset(InPreset, OutValue) && OutValue->IsObject())
	{
		const TSharedPtr<FPropertyAnimatorCorePresetObjectArchive> AnimatorArchive = OutValue->AsMutableObject();

		AnimatorArchive->Set(GET_MEMBER_NAME_STRING_CHECKED(UPropertyAnimatorClock, DisplayFormat), DisplayFormat);

		return true;
	}

	return false;
}
