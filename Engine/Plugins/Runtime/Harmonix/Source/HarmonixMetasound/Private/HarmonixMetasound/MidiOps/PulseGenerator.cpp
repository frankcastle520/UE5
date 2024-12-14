// Copyright Epic Games, Inc. All Rights Reserved.

#include "HarmonixMetasound/MidiOps/PulseGenerator.h"

namespace Harmonix::Midi::Ops
{
	void FPulseGenerator::Enable(const bool bEnable)
	{
		Enabled = bEnable;
	}

	void FPulseGenerator::SetInterval(const FMusicTimeInterval& NewInterval)
	{
		Interval = NewInterval;

		// Multiplier should be >= 1
		Interval.IntervalMultiplier = FMath::Max(Interval.IntervalMultiplier, static_cast<uint16>(1));
	}

	void FPulseGenerator::Reset()
	{
		NextPulseTimestamp = { -1, -1 };
	}

	void FPulseGenerator::Process(const HarmonixMetasound::FMidiClock& MidiClock, const TFunctionRef<void(const FPulseInfo&)>& OnPulse)
	{
		bool bIntervalIsValid = Interval.Interval != EMidiClockSubdivisionQuantization::None;
		bool bTimeSigIsValid = CurrentTimeSignature.Numerator > 0 && CurrentTimeSignature.Denominator > 0;
		
		// ensure the pulse generator is lined up with the current clock phase
		if (NextPulseTimestamp == FMusicTimestamp(-1, -1))
		{
			CurrentTimeSignature = *MidiClock.GetSongMapEvaluator().GetTimeSignatureAtTick(MidiClock.GetLastProcessedMidiTick());
			bTimeSigIsValid = CurrentTimeSignature.Numerator > 0 && CurrentTimeSignature.Denominator > 0;
			
			// Find the next pulse and line up phase with the current bar
			const FMusicTimestamp ClockCurrentTimestamp = MidiClock.GetMusicTimestampAtBlockOffset(0);
			NextPulseTimestamp.Bar = ClockCurrentTimestamp.Bar;
			NextPulseTimestamp.Beat = 1;
			IncrementTimestampByOffset(NextPulseTimestamp, Interval, CurrentTimeSignature);
			while (bIntervalIsValid && bTimeSigIsValid && NextPulseTimestamp < ClockCurrentTimestamp)
			{
				IncrementTimestampByInterval(NextPulseTimestamp, Interval, CurrentTimeSignature);
			}
		}
		
		using namespace HarmonixMetasound;
		using namespace HarmonixMetasound::MidiClockMessageTypes;

		for (const FMidiClockEvent& ClockEvent : MidiClock.GetMidiClockEventsInBlock())
		{
			if (const FAdvance* AsAdvance = ClockEvent.TryGet<FAdvance>())
			{
				if (!NextPulseTimestamp.IsValid())
				{
					return;
				}
				
				int32 NextPulseTick = MidiClock.GetSongMapEvaluator().MusicTimestampToTick(NextPulseTimestamp);

				while (bIntervalIsValid && bTimeSigIsValid && AsAdvance->LastTickToProcess() >= NextPulseTick)
				{
					OnPulse({ ClockEvent.BlockFrameIndex, NextPulseTick });

					IncrementTimestampByInterval(NextPulseTimestamp, Interval, CurrentTimeSignature);

					NextPulseTick = MidiClock.GetSongMapEvaluator().MusicTimestampToTick(NextPulseTimestamp);
				}
			}
			else if (const FTimeSignatureChange* AsTimeSigChange = ClockEvent.TryGet<FTimeSignatureChange>())
			{
				CurrentTimeSignature = AsTimeSigChange->TimeSignature;
				bTimeSigIsValid = CurrentTimeSignature.Numerator > 0 && CurrentTimeSignature.Denominator > 0;
				
				// Time sig changes will come on the downbeat, and if we change time signature,
				// we want to reset the pulse, so the next pulse is now plus the offset
				NextPulseTimestamp = MidiClock.GetSongMapEvaluator().TickToMusicTimestamp(AsTimeSigChange->Tick);
				IncrementTimestampByOffset(NextPulseTimestamp, Interval, CurrentTimeSignature);
			}
			else if (const FLoop* AsLoop = ClockEvent.TryGet<FLoop>())
			{
				// We assume the pulse should be reset on loop, and the loop start should imply the phase of the pulse
				NextPulseTimestamp = MidiClock.GetSongMapEvaluator().TickToMusicTimestamp(AsLoop->FirstTickInLoop);
				IncrementTimestampByOffset(NextPulseTimestamp, Interval, CurrentTimeSignature);
			}
			else if (const FSeek* AsSeek = ClockEvent.TryGet<FSeek>())
			{
				// When we seek, reset the pulse phase to the current bar
				const FMusicTimestamp ClockCurrentTimestamp = MidiClock.GetSongMapEvaluator().TickToMusicTimestamp(AsSeek->NewNextTick);
				NextPulseTimestamp.Bar = ClockCurrentTimestamp.Bar;
				NextPulseTimestamp.Beat = 1;
				IncrementTimestampByOffset(NextPulseTimestamp, Interval, CurrentTimeSignature);
				while (bIntervalIsValid && bTimeSigIsValid && NextPulseTimestamp < ClockCurrentTimestamp)
				{
					IncrementTimestampByInterval(NextPulseTimestamp, Interval, CurrentTimeSignature);
				}
			}
		}
	}

	void FMidiPulseGenerator::Reset()
	{
		FPulseGenerator::Reset();

		LastNoteOn.Reset();
	}

	void FMidiPulseGenerator::Process(const HarmonixMetasound::FMidiClock& MidiClock, HarmonixMetasound::FMidiStream& OutStream)
	{
		OutStream.PrepareBlock();

		// kill any notes if the interval becomes invalid
		if (GetInterval().Interval == EMidiClockSubdivisionQuantization::None && LastNoteOn.IsSet())
		{
			check(LastNoteOn->MidiMessage.IsNoteOn());
			const int32 NoteOffSample = 0;

			// Trigger the note off one tick before the note on
			const int32 NoteOffTick = MidiClock.GetNextTickToProcessAtBlockFrame(0);

			FMidiMsg Msg{ FMidiMsg::CreateAllNotesOff() };
			HarmonixMetasound::FMidiStreamEvent Event{ &VoiceGenerator, Msg };
			Event.BlockSampleFrameIndex = NoteOffSample;
			Event.AuthoredMidiTick = NoteOffTick;
			Event.CurrentMidiTick = NoteOffTick;
			Event.TrackIndex = 1;
			OutStream.InsertMidiEvent(Event);

			LastNoteOn.Reset();	
		}

		FPulseGenerator::Process(MidiClock, [this, &OutStream](const FPulseInfo& Pulse)
		{
			AddPulseNote(Pulse.BlockFrameIndex, Pulse.Tick, OutStream);
		});
	}

	void FMidiPulseGenerator::AddPulseNote(const int32 BlockFrameIndex, const int32 PulseTick, HarmonixMetasound::FMidiStream& OutStream)
	{
		int32 NoteOnSample = BlockFrameIndex;

		// Note off if there was a previous note on
		if (LastNoteOn.IsSet())
		{
			check(LastNoteOn->MidiMessage.IsNoteOn());

			// Trigger the note off one sample before the note on
			const int32 NoteOffSample = BlockFrameIndex > 0 ? BlockFrameIndex - 1 : BlockFrameIndex;
			NoteOnSample = NoteOffSample + 1;

			// Trigger the note off one tick before the note on
			const int32 NoteOffTick = PulseTick - 1;

			FMidiMsg Msg{ FMidiMsg::CreateNoteOff(LastNoteOn->MidiMessage.GetStdChannel(), LastNoteOn->MidiMessage.GetStdData1()) };
			HarmonixMetasound::FMidiStreamEvent Event{ &VoiceGenerator, Msg };
			Event.BlockSampleFrameIndex = NoteOffSample;
			Event.AuthoredMidiTick = NoteOffTick;
			Event.CurrentMidiTick = NoteOffTick;
			Event.TrackIndex = LastNoteOn->TrackIndex;
			OutStream.InsertMidiEvent(Event);

			LastNoteOn.Reset();
		}

		// Note on
		if (Enabled)
		{
			FMidiMsg Msg{ FMidiMsg::CreateNoteOn(Channel - 1, NoteNumber, Velocity) };
			HarmonixMetasound::FMidiStreamEvent Event{ &VoiceGenerator, Msg };
			Event.BlockSampleFrameIndex = NoteOnSample;
			Event.AuthoredMidiTick = PulseTick;
			Event.CurrentMidiTick = PulseTick;
			Event.TrackIndex = Track;
			OutStream.InsertMidiEvent(Event);

			LastNoteOn.Emplace(MoveTemp(Event));
		}
	}
}
