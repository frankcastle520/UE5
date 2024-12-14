// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Properties/PropertyAnimatorCoreData.h"
#include "Properties/PropertyAnimatorCoreResolver.h"
#include "PropertyAnimatorTextResolver.generated.h"

UENUM()
enum class EPropertyAnimatorTextResolverRangeUnit : uint8
{
	Percentage,
	Character,
	Word
};

UENUM()
enum class EPropertyAnimatorTextResolverRangeDirection : uint8
{
	LeftToRight,
	RightToLeft,
	FromCenter
};

/**
 * Text characters properties resolver
 * Since each character in text are transient and regenerated on change
 * We need to have a resolver that will resolve each character in the text when needed
 * We manipulate a single property that underneath means we manipulate all text characters properties
 */
UCLASS()
class UPropertyAnimatorTextResolver : public UPropertyAnimatorCoreResolver
{
	GENERATED_BODY()

public:
	UPropertyAnimatorTextResolver()
		: UPropertyAnimatorCoreResolver(TEXT("TextChars"))
	{}

	PROPERTYANIMATOR_API void SetUnit(EPropertyAnimatorTextResolverRangeUnit InUnit);
	EPropertyAnimatorTextResolverRangeUnit GetUnit() const
	{
		return Unit;
	}

	PROPERTYANIMATOR_API void SetStart(float InRangeStart);
	float GetStart() const
	{
		return Start;
	}

	PROPERTYANIMATOR_API void SetEnd(float InRangeEnd);
	float GetEnd() const
	{
		return End;
	}

	PROPERTYANIMATOR_API void SetOffset(float InRangeOffset);
	float GetOffset() const
	{
		return Offset;
	}

	PROPERTYANIMATOR_API void SetCharacterStartIndex(int32 InRangeStart);
	int32 GetCharacterStartIndex() const
	{
		return CharacterStartIndex;
	}

	PROPERTYANIMATOR_API void SetCharacterEndIndex(int32 InRangeEnd);
	int32 GetCharacterEndIndex() const
	{
		return CharacterEndIndex;
	}

	PROPERTYANIMATOR_API void SetCharacterOffsetIndex(int32 InRangeOffset);
	int32 GetCharacterOffsetIndex() const
	{
		return CharacterOffsetIndex;
	}

	PROPERTYANIMATOR_API void SetWordStartIndex(int32 InRangeStart);
	int32 GetWordStartIndex() const
	{
		return WordStartIndex;
	}

	PROPERTYANIMATOR_API void SetWordEndIndex(int32 InRangeEnd);
	int32 GetWordEndIndex() const
	{
		return WordEndIndex;
	}

	PROPERTYANIMATOR_API void SetWordOffsetIndex(int32 InRangeOffset);
	int32 GetWordOffsetIndex() const
	{
		return WordOffsetIndex;
	}

	PROPERTYANIMATOR_API void SetDirection(EPropertyAnimatorTextResolverRangeDirection InDirection);
	EPropertyAnimatorTextResolverRangeDirection GetDirection() const
	{
		return Direction;
	}

	//~ Begin UPropertyAnimatorCoreResolver
	virtual void GetResolvableProperties(const FPropertyAnimatorCoreData& InParentProperty, TSet<FPropertyAnimatorCoreData>& OutProperties) override;
	virtual void ResolveProperties(const FPropertyAnimatorCoreData& InTemplateProperty, TArray<FPropertyAnimatorCoreData>& OutProperties, bool bInForEvaluation) override;
	virtual bool ImportPreset(const UPropertyAnimatorCorePresetBase* InPreset, const TSharedRef<FPropertyAnimatorCorePresetArchive>& InValue) override;
	virtual bool ExportPreset(const UPropertyAnimatorCorePresetBase* InPreset, TSharedPtr<FPropertyAnimatorCorePresetArchive>& OutValue) const override;
	//~ End UPropertyAnimatorCoreResolver

protected:
	UPROPERTY(EditInstanceOnly, Setter, Getter, Category="Animator")
	EPropertyAnimatorTextResolverRangeUnit Unit = EPropertyAnimatorTextResolverRangeUnit::Percentage;

	UPROPERTY(EditInstanceOnly, Setter, Getter, Category="Animator", meta=(ClampMin="0", ClampMax="100", Units=Percent, EditCondition="Unit == EPropertyAnimatorTextResolverRangeUnit::Percentage", EditConditionHides))
	float Start = 0.f;

	UPROPERTY(EditInstanceOnly, Setter, Getter, Category="Animator", meta=(ClampMin="0", ClampMax="100", Units=Percent, EditCondition="Unit == EPropertyAnimatorTextResolverRangeUnit::Percentage", EditConditionHides))
	float End = 100.f;

	UPROPERTY(EditInstanceOnly, Setter, Getter, Category="Animator", meta=(Units=Percent, Delta="1", EditCondition="Unit == EPropertyAnimatorTextResolverRangeUnit::Percentage", EditConditionHides))
	float Offset = 0.f;

	UPROPERTY(EditInstanceOnly, Setter, Getter, Category="Animator", meta=(ClampMin="0", EditCondition="Unit == EPropertyAnimatorTextResolverRangeUnit::Character", EditConditionHides))
	int32 CharacterStartIndex = 0;

	UPROPERTY(EditInstanceOnly, Setter, Getter, Category="Animator", meta=(ClampMin="0", EditCondition="Unit == EPropertyAnimatorTextResolverRangeUnit::Character", EditConditionHides))
	int32 CharacterEndIndex = 100;

	UPROPERTY(EditInstanceOnly, Setter, Getter, Category="Animator", meta=(EditCondition="Unit == EPropertyAnimatorTextResolverRangeUnit::Character", EditConditionHides))
	int32 CharacterOffsetIndex = 0;

	UPROPERTY(EditInstanceOnly, Setter, Getter, Category="Animator", meta=(ClampMin="0", EditCondition="Unit == EPropertyAnimatorTextResolverRangeUnit::Word", EditConditionHides))
	int32 WordStartIndex = 0;

	UPROPERTY(EditInstanceOnly, Setter, Getter, Category="Animator", meta=(ClampMin="0", EditCondition="Unit == EPropertyAnimatorTextResolverRangeUnit::Word", EditConditionHides))
	int32 WordEndIndex = 100;

	UPROPERTY(EditInstanceOnly, Setter, Getter, Category="Animator", meta=(EditCondition="Unit == EPropertyAnimatorTextResolverRangeUnit::Word", EditConditionHides))
	int32 WordOffsetIndex = 0;

	UPROPERTY(EditInstanceOnly, Setter, Getter, Category="Animator")
	EPropertyAnimatorTextResolverRangeDirection Direction = EPropertyAnimatorTextResolverRangeDirection::LeftToRight;
};