// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once 

#include "HAL/Platform.h"
#include "Styling/SlateTypes.h"

namespace UE::Audio::Insights
{
	class FMuteSoloFilter
	{
	public:
		FMuteSoloFilter();
		~FMuteSoloFilter();
		
	private:
		void FilterMuteSolo(ECheckBoxState InMuteState, ECheckBoxState InSoloState, const FString& InCurrentFilterString) const;
	};
} // namespace UE::Audio::Insights
