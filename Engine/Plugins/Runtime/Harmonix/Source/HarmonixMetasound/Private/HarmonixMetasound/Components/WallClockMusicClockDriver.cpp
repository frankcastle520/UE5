// Copyright Epic Games, Inc. All Rights Reserved.
#include "WallClockMusicClockDriver.h"
#include "Harmonix.h"
#include "Engine/World.h"

bool FWallClockMusicClockDriver::CalculateSongPosWithOffset(float MsOffset, ECalibratedMusicTimebase Timebase, FMidiSongPos& OutResult) const
{
	check(IsInGameThread());
	if (!TempoMapMidi.IsValid())
	{
		return false;
	}

	const FSongMaps* Maps = TempoMapMidi->GetSongMaps();
	switch (Timebase)
	{
	case ECalibratedMusicTimebase::AudioRenderTime:
		OutResult.SetByTime((ClockComponent->CurrentSmoothedAudioRenderSongPos.SecondsIncludingCountIn * 1000.0f) + MsOffset, *Maps);
		break;
	case ECalibratedMusicTimebase::ExperiencedTime:
		OutResult.SetByTime((ClockComponent->CurrentPlayerExperiencedSongPos.SecondsIncludingCountIn * 1000.0f) + MsOffset, *Maps);
		break;
	case ECalibratedMusicTimebase::VideoRenderTime:
	default:
		OutResult.SetByTime((ClockComponent->CurrentVideoRenderSongPos.SecondsIncludingCountIn * 1000.0f) + MsOffset, *Maps);
		break;
	}

	return true;
}

void FWallClockMusicClockDriver::Disconnect()
{
	TempoMapMidi = nullptr;
}

bool FWallClockMusicClockDriver::RefreshCurrentSongPos()
{
	check(IsInGameThread());
	check(ClockComponent);
	check(ClockComponent->GetWorld());

	bool TempoChanged = ClockComponent->CurrentSmoothedAudioRenderSongPos.Tempo != ClockComponent->Tempo;

	double RunTime = ClockComponent->GetWorld()->GetTimeSeconds() - StartTimeSecs;

	const ISongMapEvaluator* Maps = GetCurrentSongMapEvaluator();
	check(Maps);

	ClockComponent->CurrentSmoothedAudioRenderSongPos.SetByTime((float)(RunTime * 1000.0), *Maps);
	ClockComponent->CurrentPlayerExperiencedSongPos.SetByTime(ClockComponent->CurrentSmoothedAudioRenderSongPos.SecondsIncludingCountIn * 1000.0f - FHarmonixModule::GetMeasuredUserExperienceAndReactionToAudioRenderOffsetMs(), *Maps);
	ClockComponent->CurrentVideoRenderSongPos.SetByTime(ClockComponent->CurrentSmoothedAudioRenderSongPos.SecondsIncludingCountIn * 1000.0f - FHarmonixModule::GetMeasuredVideoToAudioRenderOffsetMs(), *Maps);

	if (TempoChanged)
	{
		ClockComponent->Tempo = ClockComponent->CurrentSmoothedAudioRenderSongPos.Tempo;
		ClockComponent->CurrentBeatDurationSec = (60.0f / ClockComponent->Tempo) / ClockComponent->CurrentClockAdvanceRate;
		ClockComponent->CurrentBarDurationSec = ((ClockComponent->TimeSignatureNum * ClockComponent->CurrentBeatDurationSec) / (ClockComponent->TimeSignatureDenom / 4.0f)) / ClockComponent->CurrentClockAdvanceRate;
	}

	return true;

}

void FWallClockMusicClockDriver::OnStart()
{
	check(IsInGameThread());
	check(ClockComponent);
	StartTimeSecs = ClockComponent->GetWorld()->GetTimeSeconds();
	PauseTimeSecs = 0.0;
}

void FWallClockMusicClockDriver::OnPause()
{
	check(IsInGameThread());
	check(ClockComponent);
	PauseTimeSecs = ClockComponent->GetWorld()->GetTimeSeconds();
}

void FWallClockMusicClockDriver::OnContinue()
{
	check(IsInGameThread());
	check(ClockComponent);
	double CurrentTime = ClockComponent->GetWorld()->GetTimeSeconds();
	StartTimeSecs += (CurrentTime - PauseTimeSecs);
	PauseTimeSecs = 0.0;
	RefreshCurrentSongPos();
}

const ISongMapEvaluator* FWallClockMusicClockDriver::GetCurrentSongMapEvaluator() const
{
	check(IsInGameThread());
	if (TempoMapMidi.IsValid())
	{
		return TempoMapMidi->GetSongMaps();
	}
	check(ClockComponent);
	return &ClockComponent->DefaultMaps;
}
