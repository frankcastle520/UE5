// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Analysis/MetasoundFrontendAnalyzerFactory.h"
#include "Analysis/MetasoundFrontendVertexAnalyzer.h"
#include "AudioDefines.h"
#include "Containers/Array.h"

namespace Metasound::Engine
{
	class METASOUNDENGINE_API FVertexAnalyzerAudioBusWriter : public Frontend::FVertexAnalyzerBase
	{
	public:
		using FAnalyzerOutput = Frontend::FAnalyzerOutput;
		using FCreateAnalyzerParams = Frontend::FCreateAnalyzerParams;

		static const FName& GetAnalyzerName();
		static const FName& GetDataType();
		static FName GetAnalyzerMemberName(const Audio::FDeviceId InDeviceID, const uint32 InAudioBusID);

		class METASOUNDENGINE_API FFactory : public Frontend::TVertexAnalyzerFactory<FVertexAnalyzerAudioBusWriter>
		{
		public:
			virtual const TArray<FAnalyzerOutput>& GetAnalyzerOutputs() const override
			{
				static const TArray<FAnalyzerOutput> Outputs;
				return Outputs;
			}
		};

		FVertexAnalyzerAudioBusWriter(const FCreateAnalyzerParams& InParams);
		virtual ~FVertexAnalyzerAudioBusWriter() = default;

		virtual void Execute() override;

	private:
		struct FBusAddress
		{
			Audio::FDeviceId DeviceID = 0;
			uint32 AudioBusID = 0;

			FString ToString() const;
			static FBusAddress FromString(const FString& InAnalyzerMemberName);
		};

		Audio::FPatchInput AudioBusPatchInput;
	};
} // namespace Metasound::Engine
