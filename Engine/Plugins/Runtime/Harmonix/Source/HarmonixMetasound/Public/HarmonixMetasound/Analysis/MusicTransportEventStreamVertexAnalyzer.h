// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MetasoundDataReference.h"
#include "Analysis/MetasoundFrontendAnalyzerFactory.h"
#include "Analysis/MetasoundFrontendVertexAnalyzer.h"

#include "HarmonixMetasound/DataTypes/MusicTransport.h"

namespace HarmonixMetasound::Analysis
{
	class HARMONIXMETASOUND_API FMusicTransportEventStreamVertexAnalyzer final : public Metasound::Frontend::FVertexAnalyzerBase
	{
	public:
		static const FName& GetAnalyzerName();
		static const FName& GetDataType();

		struct HARMONIXMETASOUND_API FOutputs
		{
			/**
			 * @brief Get the default output for this analyzer
			 * @return The default output
			 */
			static const Metasound::Frontend::FAnalyzerOutput& GetValue();

			static const Metasound::Frontend::FAnalyzerOutput SeekDestination;
			static const Metasound::Frontend::FAnalyzerOutput TransportEvent;
		};

		class HARMONIXMETASOUND_API FFactory final : public Metasound::Frontend::TVertexAnalyzerFactory<FMusicTransportEventStreamVertexAnalyzer>
		{
		public:
			virtual const TArray<Metasound::Frontend::FAnalyzerOutput>& GetAnalyzerOutputs() const override;

		private:
			static const TArray<Metasound::Frontend::FAnalyzerOutput> AnalyzerOutputs;
		};

		explicit FMusicTransportEventStreamVertexAnalyzer(const Metasound::Frontend::FCreateAnalyzerParams& InParams);
		virtual ~FMusicTransportEventStreamVertexAnalyzer() override = default;

		virtual void Execute() override;

	private:
		FMusicSeekTargetWriteRef  SeekDestination;
		FMusicTransportEventWriteRef LastMusicTransportEvent;
		int64 NumFrames{ 0 };
		int32 FramesPerBlock{ 0 };
		float SampleRate{ 0 };

		static const FName AnalyzerName;
	};
}
