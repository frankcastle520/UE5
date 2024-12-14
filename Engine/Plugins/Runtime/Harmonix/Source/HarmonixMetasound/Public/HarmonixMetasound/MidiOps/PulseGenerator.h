// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HarmonixDsp/Parameters/Parameter.h"

#include "HarmonixMetasound/DataTypes/MidiClock.h"
#include "HarmonixMetasound/DataTypes/MidiStream.h"
#include "HarmonixMetasound/DataTypes/MusicTimeInterval.h"

#include "HarmonixMidi/MidiVoiceId.h"

namespace Harmonix::Midi::Ops
{
	class HARMONIXMETASOUND_API FPulseGenerator
	{
	public:
		virtual ~FPulseGenerator() = default;
		
		void Enable(bool bEnable);

		void SetInterval(const FMusicTimeInterval& NewInterval);
		FMusicTimeInterval GetInterval() const { return Interval; }

		virtual void Reset();
		
		struct FPulseInfo
		{
			int32 BlockFrameIndex;
			int32 Tick;
		};
		
		void Process(const HarmonixMetasound::FMidiClock& MidiClock, const TFunctionRef<void(const FPulseInfo&)>& OnPulse);

	protected:
		bool Enabled{ true };

		FMusicTimeInterval Interval{};
		FTimeSignature CurrentTimeSignature{};
		FMusicTimestamp NextPulseTimestamp{ -1, -1 };
	};
	
	class HARMONIXMETASOUND_API FMidiPulseGenerator : public FPulseGenerator
	{
	public:
		virtual ~FMidiPulseGenerator() override = default;
		
		Dsp::Parameters::TParameter<uint8> Channel{ 1, 16, 1 };

		Dsp::Parameters::TParameter<uint16> Track{ 1, UINT16_MAX, 1 };
		
		Dsp::Parameters::TParameter<uint8> NoteNumber{ 0, 127, 60 };
		
		Dsp::Parameters::TParameter<uint8> Velocity{ 0, 127, 127 };

		virtual void Reset() override;

		void Process(const HarmonixMetasound::FMidiClock& MidiClock, HarmonixMetasound::FMidiStream& OutStream);

	private:
		void AddPulseNote(const int32 BlockFrameIndex, const int32 PulseTick, HarmonixMetasound::FMidiStream& OutStream);

		FMidiVoiceGeneratorBase VoiceGenerator{};
		TOptional<HarmonixMetasound::FMidiStreamEvent> LastNoteOn;
	};
}
