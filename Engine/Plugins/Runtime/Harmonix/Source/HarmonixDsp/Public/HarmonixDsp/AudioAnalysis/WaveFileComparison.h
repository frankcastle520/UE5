// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Audio.h"

class FString;
class FArchive;

namespace Harmonix::Dsp::AudioAnalysis 
{
	class HARMONIXDSP_API FWaveFileComparison
	{
	public:
		bool LoadForCompare(const FString& Wave1FilePath, const FString& Wave2FilePath);
		bool LoadForCompare(FArchive& Wave1Archive, FArchive& Wave2Archive);

		float GetPSNR(bool bCommonSizeOnly = true) const;

	private:
		struct FOneWaveFile
		{
			FWaveModInfo Info;
			TArray<uint8> BulkData;

			bool Load(FArchive& Archive);
		};

		FOneWaveFile Wave1;
		FOneWaveFile Wave2;

		bool bOk = false;
	};
}