// Copyright Epic Games, Inc. All Rights Reserved.

#include "TakeRecorderSourceHelpers.h"
#include "TakeRecorderSource.h"
#include "TakeRecorderActorSource.h"
#include "LevelSequenceActor.h"
#include "GameFramework/Actor.h"
#include "ScopedTransaction.h"
#include "TakeRecorderLevelSequenceSource.h"

#include "LevelSequence.h"
#include "MovieScene.h"
#include "MovieSceneTakeTrack.h"
#include "MovieSceneTakeSection.h"
#include "TakeMetaData.h"

#include "Editor.h"

#define LOCTEXT_NAMESPACE "TakeRecorderSourceHelpers"

namespace TakeRecorderSourceHelpers
{

void AddActorSources(
	UTakeRecorderSources* TakeRecorderSources,
	TArrayView<AActor* const> ActorsToRecord,
	bool                      bReduceKeys,
	bool                      bShowProgress)
{
	if (ActorsToRecord.Num() > 0)
	{
		FScopedTransaction Transaction(FText::Format(
			LOCTEXT("AddSources", "Add Recording {0}|plural(one=Source, other=Sources)"), ActorsToRecord.Num()));
		TakeRecorderSources->Modify();

		for (AActor* Actor : ActorsToRecord)
		{
			if (Actor->IsA<ALevelSequenceActor>())
			{
				ALevelSequenceActor* LevelSequenceActor = Cast<ALevelSequenceActor>(Actor);

				UTakeRecorderLevelSequenceSource* LevelSequenceSource = nullptr;

				for (UTakeRecorderSource* Source : TakeRecorderSources->GetSources())
				{
					if (Source->IsA<UTakeRecorderLevelSequenceSource>())
					{
						LevelSequenceSource = Cast<UTakeRecorderLevelSequenceSource>(Source);
						break;
					}
				}

				if (!LevelSequenceSource)
				{
					LevelSequenceSource = TakeRecorderSources->AddSource<UTakeRecorderLevelSequenceSource>();
				}

				ULevelSequence* Sequence = LevelSequenceActor->GetSequence();
				if (Sequence)
				{
					if (!LevelSequenceSource->LevelSequencesToTrigger.Contains(Sequence))
					{
						LevelSequenceSource->LevelSequencesToTrigger.Add(Sequence);
					}
				}
			}
			else
			{
				UTakeRecorderActorSource* NewSource = TakeRecorderSources->AddSource<UTakeRecorderActorSource>();

				if (AActor* EditorActor = EditorUtilities::GetEditorWorldCounterpartActor(Actor))
				{
					NewSource->Target = EditorActor;
				}
				else
				{
					NewSource->Target = Actor;
				}
				NewSource->bShowProgressDialog = bShowProgress;
				NewSource->bReduceKeys = bReduceKeys;

				// Send a PropertyChangedEvent so the class catches the callback and rebuilds the property map.
				FPropertyChangedEvent PropertyChangedEvent(UTakeRecorderActorSource::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UTakeRecorderActorSource, Target)), EPropertyChangeType::ValueSet);
				NewSource->PostEditChangeProperty(PropertyChangedEvent);
			}
		}
	}
}

void RemoveAllActorSources(UTakeRecorderSources* TakeRecorderSources)
{
	if (TakeRecorderSources->GetSources().IsEmpty())
	{
		return;
	}

	FScopedTransaction Transaction(FText::Format(LOCTEXT(
			"RemoveAllActorSources", 
			"Remove Recording {0}|plural(one=Source, other=Sources)"), TakeRecorderSources->GetSources().Num()));
	TakeRecorderSources->Modify();

	while (!TakeRecorderSources->GetSources().IsEmpty())
	{
		TakeRecorderSources->RemoveSource(TakeRecorderSources->GetSources()[0]);
	}
}

void ProcessRecordedTimes(ULevelSequence* InSequence, UMovieSceneTakeTrack* TakeTrack, const TOptional<TRange<FFrameNumber>>& FrameRange, const FArrayOfRecordedTimePairs& RecordedTimes)
{
	check(TakeTrack);
	UMovieScene* MovieScene = InSequence->GetMovieScene();

	// In case we need it later, get the earliest timecode source *before* we
	// add the take section, since its timecode source will be default
	// constructed as all zeros and might accidentally compare as earliest.
	const FMovieSceneTimecodeSource EarliestTimecodeSource = MovieScene->GetEarliestTimecodeSource();

	TakeTrack->RemoveAllAnimationData();

	UMovieSceneTakeSection* TakeSection = Cast<UMovieSceneTakeSection>(TakeTrack->CreateNewSection());
	TakeTrack->AddSection(*TakeSection);

	if (FrameRange.IsSet())
	{
		TArray<int32> Hours, Minutes, Seconds, Frames;
		TArray<FMovieSceneFloatValue> SubFrames;
		TArray<FFrameNumber> Times;

		Hours.Reserve(RecordedTimes.Num());
		Minutes.Reserve(RecordedTimes.Num());
		Seconds.Reserve(RecordedTimes.Num());
		Frames.Reserve(RecordedTimes.Num());
		SubFrames.Reserve(RecordedTimes.Num());
		Times.Reserve(RecordedTimes.Num());

		FFrameRate TickResolution = MovieScene->GetTickResolution();
		FFrameRate DisplayRate = MovieScene->GetDisplayRate();

		FFrameRate TCRate = TickResolution;
		for (const TPair<FQualifiedFrameTime, FQualifiedFrameTime>& RecordedTimePair : RecordedTimes)
		{
			FFrameNumber FrameNumber = RecordedTimePair.Key.Time.FrameNumber;
			if (!FrameRange.GetValue().Contains(FrameNumber))
			{
				continue;
			}

			FTimecode Timecode = RecordedTimePair.Value.ToTimecode();
			TCRate = RecordedTimePair.Value.Rate;
			Hours.Add(Timecode.Hours);
			Minutes.Add(Timecode.Minutes);
			Seconds.Add(Timecode.Seconds);
			Frames.Add(Timecode.Frames);

			FMovieSceneFloatValue SubFrame;
			if (RecordedTimePair.Value.Time.GetSubFrame() > 0)
			{
				// If the Timecode provided gave us a subframe value then we should use that value.  Otherwise, we should compute
				// the most appropriate value based on the timecode rate.
				SubFrame.Value = RecordedTimePair.Value.Time.GetSubFrame();
			}
			else
			{
				FFrameTime FrameTime = FFrameRate::TransformTime(RecordedTimePair.Key.Time, TickResolution, DisplayRate);
				FQualifiedFrameTime FrameTimeAsTimeCodeRate(FrameTime, TCRate);

				SubFrame.Value = FrameTimeAsTimeCodeRate.Time.GetSubFrame();
			}

			SubFrame.InterpMode = ERichCurveInterpMode::RCIM_Linear;
			SubFrames.Add(SubFrame);

			Times.Add(FrameNumber);
		}

		Hours.Shrink();
		Minutes.Shrink();
		Seconds.Shrink();
		Frames.Shrink();
		SubFrames.Shrink();
		Times.Shrink();

		TakeSection->HoursCurve.Set(Times, Hours);
		TakeSection->MinutesCurve.Set(Times, Minutes);
		TakeSection->SecondsCurve.Set(Times, Seconds);
		TakeSection->FramesCurve.Set(Times, Frames);
		TakeSection->SubFramesCurve.Set(Times, SubFrames);
		TakeSection->RateCurve.SetDefault(TCRate.AsDecimal());
	}

	// Since the take section was created post recording here in this
	// function, it wasn't available at the start of recording to have
	// its timecode source set with the other sections, so we set it here.
	if (TakeSection->HoursCurve.GetNumKeys() > 0)
	{
		// We populated the take section's timecode curves with data, so
		// use the first values as the timecode source.
		const int32 Hours = TakeSection->HoursCurve.GetValues()[0];
		const int32 Minutes = TakeSection->MinutesCurve.GetValues()[0];
		const int32 Seconds = TakeSection->SecondsCurve.GetValues()[0];
		const int32 Frames = TakeSection->FramesCurve.GetValues()[0];
		const bool bIsDropFrame = false;
		const FTimecode Timecode(Hours, Minutes, Seconds, Frames, bIsDropFrame);
		TakeSection->TimecodeSource = FMovieSceneTimecodeSource(Timecode);
	}
	else
	{
		// Otherwise, adopt the earliest timecode source from one of the movie
		// scene's other sections as the timecode source for the take section.
		// This case is unlikely.
		TakeSection->TimecodeSource = EarliestTimecodeSource;
	}

	if (UTakeMetaData* TakeMetaData = InSequence->FindMetaData<UTakeMetaData>())
	{
		TakeSection->Slate.SetDefault(FString::Printf(TEXT("%s_%d"), *TakeMetaData->GetSlate(), TakeMetaData->GetTakeNumber()));
	}

	if (TakeSection->GetAutoSizeRange().IsSet())
	{
		TakeSection->SetRange(TakeSection->GetAutoSizeRange().GetValue());
	}
}

}

#undef LOCTEXT_NAMESPACE // "TakeRecorderSourceHelpers"
