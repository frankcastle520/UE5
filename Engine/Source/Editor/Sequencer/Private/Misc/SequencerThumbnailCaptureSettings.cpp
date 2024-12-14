// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/SequencerThumbnailCaptureSettings.h"

#include "ISequencer.h"
#include "MovieScene.h"
#include "Math/Range.h"

namespace UE::Sequencer
{
	FFrameNumber GetFrameByRule(const ISequencer& Sequencer, ESequencerThumbnailCaptureTimeLocation Rule)
	{
		switch (Rule)
		{
		case ESequencerThumbnailCaptureTimeLocation::FirstFrame:
			return Sequencer.GetRootMovieSceneSequence()->GetMovieScene()->GetPlaybackRange().GetLowerBoundValue();
		case ESequencerThumbnailCaptureTimeLocation::MiddleFrame:
			{
				const FFrameNumber FirstFrame = GetFrameByRule(Sequencer, ESequencerThumbnailCaptureTimeLocation::FirstFrame);
				const FFrameNumber LastFrame = GetFrameByRule(Sequencer, ESequencerThumbnailCaptureTimeLocation::LastFrame);
				return FirstFrame + (LastFrame - FirstFrame) / 2;
			}
		case ESequencerThumbnailCaptureTimeLocation::LastFrame:
			return Sequencer.GetRootMovieSceneSequence()->GetMovieScene()->GetPlaybackRange().GetUpperBoundValue();
			
		case ESequencerThumbnailCaptureTimeLocation::CurrentFrame:
			return Sequencer.GetGlobalTime().Time.FrameNumber;
			
		default:
			checkNoEntry();
			return GetFrameByRule(Sequencer, ESequencerThumbnailCaptureTimeLocation::CurrentFrame);
		}
	}
}
