// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetasoundNodeInterface.h"

#include "MetasoundExecutableOperator.h"
#include "MetasoundFacade.h"
#include "MetasoundNodeInterface.h"
#include "MetasoundParamHelper.h"
#include "MetasoundSampleCounter.h"
#include "MetasoundStandardNodesCategories.h"
#include "MetasoundVertex.h"

#include "HarmonixMetasound/Common.h"
#include "HarmonixMetasound/DataTypes/MidiClock.h"
#include "HarmonixMetasound/DataTypes/MusicTransport.h"

namespace HarmonixMetasound::Nodes::MetronomeNode
{
	using namespace Metasound;

	//HARMONIXMETASOUND_API Metasound::FNodeClassName GetClassName();
	//HARMONIXMETASOUND_API int32 GetCurrentMajorVersion();

	class HARMONIXMETASOUND_API FMetronomeOperatorBase : public TExecutableOperator<FMetronomeOperatorBase>, public FMusicTransportControllable
	{
	public:

		FMetronomeOperatorBase(const FBuildOperatorParams& InParams,
			const FMusicTransportEventStreamReadRef& InTransport,
			const bool  InLoop,
			const int32 InLoopLengthBars,
			const FInt32ReadRef& InTimSigNumerator,
			const FInt32ReadRef& InTimeSigDenominator,
			const FFloatReadRef& InTempo,
			const FFloatReadRef& InSpeedMultiplier,
			const int32 InSeekPrerollBars);

		virtual void BindInputs(FInputVertexInterfaceData& InVertexData) override;
		virtual void BindOutputs(FOutputVertexInterfaceData& InVertexData) override;

		void Reset(const FResetParams& Params);
		void Execute();

	protected:
		void Init();

		//** INPUTS
		FMusicTransportEventStreamReadRef TransportInPin;
		const bool  LoopInPin;
		const int32 LoopLengthBarsInPin;
		FInt32ReadRef TimeSigNumInPin;
		FInt32ReadRef TimeSigDenomInPin;
		FFloatReadRef TempoInPin;
		FFloatReadRef SpeedMultInPin;
		const int32 SeekPreRollBarsInPin;

		//** OUTPUTS
		FMidiClockWriteRef MidiClockOutPin;

		//** DATA
		TSharedPtr<FMidiClock, ESPMode::NotThreadSafe> MonotonicallyIncreasingClock;
		TSharedPtr<FSongMaps> SongMaps;
		FSampleCount BlockSize;
		float        SampleRate;
		float        CurrentTempo;
		int32        CurrentTimeSigNum;
		int32        CurrentTimeSigDenom;
		int32		 LastProcessedClockTick = -1;
		int32		 NextClockTickToProcess = 0;
		bool		 bClocksArePreparedForExecute = true;

		void BuildSongMaps(bool ResetToStart = true);
		void UpdateMidi();
		void AddTempoChangeForMidi(float TempoBPM);
		virtual void HandleTimeSigChangeForMidi(int32 TimeSigNum, int32 TimeSigDenom);
		void HandleTransportChange(int32 StartFrameIndex, EMusicPlayerTransportState NewTransportState);
		void PrepareClocksForExecute();
		void MarkClocksAsExecuted();

		FMidiClock& GetDrivingMidiClock() { return LoopInPin ? *MonotonicallyIncreasingClock : (*MidiClockOutPin); }
	};
}