// Copyright Epic Games, Inc. All Rights Reserved.

#include "Properties/PropertyAnimatorTextResolver.h"

#include "Presets/PropertyAnimatorCorePresetArchive.h"
#include "Text3DComponent.h"

void UPropertyAnimatorTextResolver::SetUnit(EPropertyAnimatorTextResolverRangeUnit InUnit)
{
	Unit = InUnit;
}

void UPropertyAnimatorTextResolver::SetStart(float InRangeStart)
{
	Start = FMath::Clamp(InRangeStart, 0, 100);
}

void UPropertyAnimatorTextResolver::SetEnd(float InRangeEnd)
{
	End = FMath::Clamp(InRangeEnd, 0, 100);
}

void UPropertyAnimatorTextResolver::SetOffset(float InRangeOffset)
{
	Offset = InRangeOffset;
}

void UPropertyAnimatorTextResolver::SetCharacterStartIndex(int32 InRangeStart)
{
	CharacterStartIndex = FMath::Max(InRangeStart, 0);
}

void UPropertyAnimatorTextResolver::SetCharacterEndIndex(int32 InRangeEnd)
{
	CharacterEndIndex = FMath::Max(InRangeEnd, 0);
}

void UPropertyAnimatorTextResolver::SetCharacterOffsetIndex(int32 InRangeOffset)
{
	CharacterOffsetIndex = InRangeOffset;
}

void UPropertyAnimatorTextResolver::SetWordStartIndex(int32 InRangeStart)
{
	WordStartIndex = FMath::Max(InRangeStart, 0);
}

void UPropertyAnimatorTextResolver::SetWordEndIndex(int32 InRangeEnd)
{
	WordEndIndex = FMath::Max(InRangeEnd, 0);
}

void UPropertyAnimatorTextResolver::SetWordOffsetIndex(int32 InRangeOffset)
{
	WordOffsetIndex = InRangeOffset;
}

void UPropertyAnimatorTextResolver::SetDirection(EPropertyAnimatorTextResolverRangeDirection InDirection)
{
	Direction = InDirection;
}

void UPropertyAnimatorTextResolver::GetResolvableProperties(const FPropertyAnimatorCoreData& InParentProperty, TSet<FPropertyAnimatorCoreData>& OutProperties)
{
	const AActor* Actor = InParentProperty.GetOwningActor();
	if (!Actor || InParentProperty.IsResolved())
	{
		return;
	}

	UText3DComponent* TextComponent = Actor->FindComponentByClass<UText3DComponent>();
	if (!TextComponent)
	{
		return;
	}

	USceneComponent* TextRootComponent = TextComponent->GetChildComponent(1);
	if (!TextRootComponent)
	{
		return;
	}

	FProperty* RelativeLocation = FindFProperty<FProperty>(TextRootComponent->GetClass(), TEXT("RelativeLocation"));
	FPropertyAnimatorCoreData RelativeLocationProperty(TextRootComponent, RelativeLocation, nullptr, GetClass());
	OutProperties.Add(RelativeLocationProperty);

	FProperty* RelativeRotation = FindFProperty<FProperty>(TextRootComponent->GetClass(), TEXT("RelativeRotation"));
	FPropertyAnimatorCoreData RelativeRotationProperty(TextRootComponent, RelativeRotation, nullptr, GetClass());
	OutProperties.Add(RelativeRotationProperty);

	FProperty* RelativeScale = FindFProperty<FProperty>(TextRootComponent->GetClass(), TEXT("RelativeScale3D"));
	FPropertyAnimatorCoreData RelativeScaleProperty(TextRootComponent, RelativeScale, nullptr, GetClass());
	OutProperties.Add(RelativeScaleProperty);
}

void UPropertyAnimatorTextResolver::ResolveProperties(const FPropertyAnimatorCoreData& InTemplateProperty, TArray<FPropertyAnimatorCoreData>& OutProperties, bool bInForEvaluation)
{
	if (!InTemplateProperty.IsResolvable())
	{
		return;
	}

	const USceneComponent* TextRootComponent = Cast<USceneComponent>(InTemplateProperty.GetOwningComponent());
	if (!TextRootComponent)
	{
		return;
	}

	const TArray<FProperty*> ChainProperties = InTemplateProperty.GetChainProperties();

	// Gather each character in the text
	for (int32 ComponentIndex = 0; ComponentIndex < TextRootComponent->GetNumChildrenComponents(); ComponentIndex++)
	{
		USceneComponent* CharacterKerningComponent = TextRootComponent->GetChildComponent(ComponentIndex);

		if (!CharacterKerningComponent)
		{
			continue;
		}

		FPropertyAnimatorCoreData CharacterProperty(CharacterKerningComponent, ChainProperties);
		OutProperties.Add(CharacterProperty);
	}

	if (!bInForEvaluation || OutProperties.IsEmpty())
	{
		return;
	}

	const int32 MaxIndex = OutProperties.Num();
	int32 BeginIndex = 0;
	int32 EndIndex = 0;

	switch (Unit)
	{
		case EPropertyAnimatorTextResolverRangeUnit::Percentage:
		{
			float StartPercentage = Start / 100.f;
			float EndPercentage = End / 100.f;
			float OffsetPercentage = Offset / 100.f;

			if (Direction == EPropertyAnimatorTextResolverRangeDirection::RightToLeft)
			{
				const float Temp = 1.f - StartPercentage;
				StartPercentage = 1.f - EndPercentage;
				EndPercentage = Temp;
				OffsetPercentage *= -1;
			}
			else if (Direction == EPropertyAnimatorTextResolverRangeDirection::FromCenter)
			{
				constexpr float MidPercentage = 0.5f;
				const float Expansion = EndPercentage / 2.f;
				StartPercentage = MidPercentage - Expansion;
				EndPercentage = MidPercentage + Expansion;
			}

			BeginIndex = StartPercentage * MaxIndex + OffsetPercentage * MaxIndex;
			EndIndex = EndPercentage * MaxIndex + OffsetPercentage * MaxIndex;
		}
		break;

		case EPropertyAnimatorTextResolverRangeUnit::Character:
		{
			int32 CharacterStart = CharacterStartIndex;
			int32 CharacterEnd = CharacterEndIndex;
			int32 CharacterOffset = CharacterOffsetIndex;

			if (Direction == EPropertyAnimatorTextResolverRangeDirection::RightToLeft)
			{
				const int32 Temp = MaxIndex - CharacterStart;
				CharacterStart = MaxIndex - CharacterEnd;
				CharacterEnd = Temp;
				CharacterOffset *= -1;
			}
			else if (Direction == EPropertyAnimatorTextResolverRangeDirection::FromCenter)
			{
				const int32 CharacterMid = MaxIndex / 2;
				const int32 Expansion = CharacterEnd / 2;
				CharacterStart = CharacterMid - Expansion;
				CharacterEnd = CharacterMid + Expansion;
			}

			BeginIndex = CharacterStart + CharacterOffset;
			EndIndex = CharacterEnd + CharacterOffset;
		}
		break;

		case EPropertyAnimatorTextResolverRangeUnit::Word:
		{
			if (const UText3DComponent* TextComponent = TextRootComponent->GetTypedOuter<UText3DComponent>())
			{
				const FText3DStatistics& TextStats = TextComponent->GetStatistics();

				if (TextStats.Words.IsEmpty())
				{
					break;
				}

				const int32 WordCount = TextStats.Words.Num();
				int32 WordStart = WordStartIndex;
				int32 WordEnd = WordEndIndex;
				int32 WordOffset = WordOffsetIndex;

				if (Direction == EPropertyAnimatorTextResolverRangeDirection::RightToLeft)
				{
					const int32 Temp = WordCount - WordStart;
					WordStart = WordCount - WordEnd;
					WordEnd = Temp;
					WordOffset *= -1;
				}
				else if (Direction == EPropertyAnimatorTextResolverRangeDirection::FromCenter)
				{
					const int32 WordMid = FMath::CeilToInt(WordCount / 2.f);
					const int32 Expansion = FMath::CeilToInt(WordEnd / 2.f);
					WordStart = WordMid - Expansion;
					WordEnd = WordMid + Expansion;
				}

				if (WordStart != WordEnd)
				{
					WordStart += WordOffset;
					WordEnd += WordOffset - 1;

					if (TextStats.Words.IsValidIndex(WordStart))
					{
						BeginIndex = TextStats.Words[WordStart].RenderRange.BeginIndex;
					}

					if (TextStats.Words.IsValidIndex(WordEnd))
					{
						EndIndex = TextStats.Words[WordEnd].RenderRange.EndIndex;
					}
					else if (WordEnd >= WordCount && WordStart < WordCount)
					{
						EndIndex = TextStats.Words.Last().RenderRange.EndIndex;
					}
				}
			}
		}
		break;
	}

	if (EndIndex < 0 || BeginIndex > EndIndex || BeginIndex == EndIndex || BeginIndex > MaxIndex)
	{
		OutProperties.Empty();
		return;
	}

	// Remove at the end
	if (EndIndex < MaxIndex)
	{
		OutProperties.RemoveAt(EndIndex, MaxIndex - EndIndex);
	}

	// Remove at the start
	if (BeginIndex > 0 && BeginIndex <= MaxIndex)
	{
		OutProperties.RemoveAt(0, BeginIndex);
	}
}

bool UPropertyAnimatorTextResolver::ImportPreset(const UPropertyAnimatorCorePresetBase* InPreset, const TSharedRef<FPropertyAnimatorCorePresetArchive>& InValue)
{
	if (Super::ImportPreset(InPreset, InValue) && InValue->IsObject())
	{
		const TSharedPtr<FPropertyAnimatorCorePresetObjectArchive> ResolverArchive = InValue->AsMutableObject();

		uint64 UnitValue = static_cast<uint64>(Unit);
		ResolverArchive->Get(GET_MEMBER_NAME_STRING_CHECKED(UPropertyAnimatorTextResolver, Unit), UnitValue);
		SetUnit(static_cast<EPropertyAnimatorTextResolverRangeUnit>(UnitValue));

		double StartValue = Start;
		ResolverArchive->Get(GET_MEMBER_NAME_STRING_CHECKED(UPropertyAnimatorTextResolver, Start), StartValue);
		SetStart(StartValue);

		double EndValue = End;
		ResolverArchive->Get(GET_MEMBER_NAME_STRING_CHECKED(UPropertyAnimatorTextResolver, End), EndValue);
		SetEnd(EndValue);

		double OffsetValue = Offset;
		ResolverArchive->Get(GET_MEMBER_NAME_STRING_CHECKED(UPropertyAnimatorTextResolver, Offset), OffsetValue);
		SetOffset(OffsetValue);

		int64 CharStartIndexValue = CharacterStartIndex;
		ResolverArchive->Get(GET_MEMBER_NAME_STRING_CHECKED(UPropertyAnimatorTextResolver, CharacterStartIndex), CharStartIndexValue);
		SetCharacterStartIndex(CharStartIndexValue);

		int64 CharEndIndexValue = CharacterEndIndex;
		ResolverArchive->Get(GET_MEMBER_NAME_STRING_CHECKED(UPropertyAnimatorTextResolver, CharacterEndIndex), CharEndIndexValue);
		SetCharacterEndIndex(CharEndIndexValue);

		int64 CharOffsetIndexValue = CharacterOffsetIndex;
		ResolverArchive->Get(GET_MEMBER_NAME_STRING_CHECKED(UPropertyAnimatorTextResolver, CharacterOffsetIndex), CharOffsetIndexValue);
		SetCharacterOffsetIndex(CharOffsetIndexValue);

		int64 WordStartIndexValue = WordStartIndex;
		ResolverArchive->Get(GET_MEMBER_NAME_STRING_CHECKED(UPropertyAnimatorTextResolver, WordStartIndex), WordStartIndexValue);
		SetWordStartIndex(WordStartIndexValue);

		int64 WordEndIndexValue = WordEndIndex;
		ResolverArchive->Get(GET_MEMBER_NAME_STRING_CHECKED(UPropertyAnimatorTextResolver, WordEndIndex), WordEndIndexValue);
		SetWordEndIndex(WordEndIndexValue);

		int64 WordOffsetIndexValue = WordOffsetIndex;
		ResolverArchive->Get(GET_MEMBER_NAME_STRING_CHECKED(UPropertyAnimatorTextResolver, WordOffsetIndex), WordOffsetIndexValue);
		SetWordOffsetIndex(WordOffsetIndexValue);

		uint64 DirectionValue = static_cast<uint64>(Direction);
		ResolverArchive->Get(GET_MEMBER_NAME_STRING_CHECKED(UPropertyAnimatorTextResolver, Direction), DirectionValue);
		SetDirection(static_cast<EPropertyAnimatorTextResolverRangeDirection>(WordOffsetIndexValue));

		return true;
	}

	return false;
}

bool UPropertyAnimatorTextResolver::ExportPreset(const UPropertyAnimatorCorePresetBase* InPreset, TSharedPtr<FPropertyAnimatorCorePresetArchive>& OutValue) const
{
	if (Super::ExportPreset(InPreset, OutValue) && OutValue->IsObject())
	{
		const TSharedPtr<FPropertyAnimatorCorePresetObjectArchive> ResolverArchive = OutValue->AsMutableObject();

		ResolverArchive->Set(GET_MEMBER_NAME_STRING_CHECKED(UPropertyAnimatorTextResolver, Unit), static_cast<uint64>(Unit));
		ResolverArchive->Set(GET_MEMBER_NAME_STRING_CHECKED(UPropertyAnimatorTextResolver, Start), Start);
		ResolverArchive->Set(GET_MEMBER_NAME_STRING_CHECKED(UPropertyAnimatorTextResolver, End), End);
		ResolverArchive->Set(GET_MEMBER_NAME_STRING_CHECKED(UPropertyAnimatorTextResolver, Offset), Offset);
		ResolverArchive->Set(GET_MEMBER_NAME_STRING_CHECKED(UPropertyAnimatorTextResolver, CharacterStartIndex), static_cast<int64>(CharacterStartIndex));
		ResolverArchive->Set(GET_MEMBER_NAME_STRING_CHECKED(UPropertyAnimatorTextResolver, CharacterEndIndex), static_cast<int64>(CharacterEndIndex));
		ResolverArchive->Set(GET_MEMBER_NAME_STRING_CHECKED(UPropertyAnimatorTextResolver, CharacterOffsetIndex), static_cast<int64>(CharacterOffsetIndex));
		ResolverArchive->Set(GET_MEMBER_NAME_STRING_CHECKED(UPropertyAnimatorTextResolver, WordStartIndex), static_cast<int64>(WordStartIndex));
		ResolverArchive->Set(GET_MEMBER_NAME_STRING_CHECKED(UPropertyAnimatorTextResolver, WordEndIndex), static_cast<int64>(WordEndIndex));
		ResolverArchive->Set(GET_MEMBER_NAME_STRING_CHECKED(UPropertyAnimatorTextResolver, WordOffsetIndex), static_cast<int64>(WordOffsetIndex));
		ResolverArchive->Set(GET_MEMBER_NAME_STRING_CHECKED(UPropertyAnimatorTextResolver, Direction), static_cast<uint64>(Direction));

		return true;
	}

	return false;
}
