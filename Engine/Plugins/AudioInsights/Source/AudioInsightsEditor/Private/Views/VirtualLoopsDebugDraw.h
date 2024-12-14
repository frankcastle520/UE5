// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once 

#include "AudioDefines.h"
#include "SoundAttenuationVisualizer.h"

namespace UE::Audio::Insights
{
	class IDashboardDataViewEntry;

	class FVirtualLoopsDebugDraw
	{
	public:
		FVirtualLoopsDebugDraw();
		~FVirtualLoopsDebugDraw();

		void DebugDraw(float InElapsed, const TArray<TSharedPtr<IDashboardDataViewEntry>>& InSelectedItems, ::Audio::FDeviceId InAudioDeviceId);

	private:
		void DebugDrawEntries(float InElapsed, const TArray<TSharedPtr<IDashboardDataViewEntry>>& InSelectedItems, ::Audio::FDeviceId InAudioDeviceId) const;

		FSoundAttenuationVisualizer AttenuationVisualizer;
	};
} // namespace UE::Audio::Insights
