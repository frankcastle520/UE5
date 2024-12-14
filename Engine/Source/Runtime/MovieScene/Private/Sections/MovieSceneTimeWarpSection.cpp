// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sections/MovieSceneTimeWarpSection.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Variants/MovieScenePlayRateCurve.h"
#include "Evaluation/MovieSceneSequenceTransform.h"

UMovieSceneTimeWarpSection::UMovieSceneTimeWarpSection(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bSupportsInfiniteRange = true;
	SetRange(TRange<FFrameNumber>::All());
}

FMovieSceneNestedSequenceTransform UMovieSceneTimeWarpSection::GenerateTransform() const
{
	FFrameNumber Offset = HasStartFrame() ? GetInclusiveStartFrame() : 0;
	return FMovieSceneNestedSequenceTransform(Offset, TimeWarp.ShallowCopy());
}

EMovieSceneChannelProxyType UMovieSceneTimeWarpSection::CacheChannelProxy()
{
	FMovieSceneChannelProxyData Channels;

	if (TimeWarp.GetType() == EMovieSceneTimeWarpType::Custom)
	{
		if (UMovieSceneTimeWarpGetter* Custom = TimeWarp.AsCustom())
		{
			Custom->PopulateChannelProxy(Channels, UMovieSceneTimeWarpGetter::EAllowTopLevelChannels::Yes);
		}
	}

	ChannelProxy = MakeShared<FMovieSceneChannelProxy>(MoveTemp(Channels));
	return EMovieSceneChannelProxyType::Dynamic;
}

FMovieSceneTimeWarpVariant* UMovieSceneTimeWarpSection::GetTimeWarp()
{
	return &TimeWarp;
}

#if WITH_EDITOR

bool UMovieSceneTimeWarpSection::Modify(bool bAlwaysMarkDirty)
{
	UMovieSceneTimeWarpGetter* Custom = TimeWarp.GetType() == EMovieSceneTimeWarpType::Custom ? TimeWarp.AsCustom() : nullptr;
	if (Custom)
	{
		Custom->Modify(bAlwaysMarkDirty);
	}
	return Super::Modify(bAlwaysMarkDirty);
}

void UMovieSceneTimeWarpSection::PostEditChangeProperty( struct FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UMovieSceneTimeWarpSection, TimeWarp))
	{
		ChannelProxy = nullptr;
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

#endif // WITH_EDITOR
