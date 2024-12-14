// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <HAL/Platform.h>

#if PLATFORM_WINDOWS

#include "Windows/WindowsPlatformTime.h"

namespace UE::StylusInput::Private::Windows
{
	class FPacketStats
	{
	public:
		void NewPacket()
		{
			const uint32 Timestamp = FPlatformTime::Cycles();

			if (Latest - Earliest >= CyclesPerSecond)
			{
				PacketsPerSecond = Earliest != Latest ? NumPackets / ((Latest - Earliest) / CyclesPerSecond) : 0.0f;
				Earliest = Timestamp;
				NumPackets = -1;
			}

			Latest = Timestamp;
			++NumPackets;
		}

		float GetPacketsPerSecond() const
		{
			const uint32 Timestamp = FPlatformTime::Cycles();
			return Timestamp - Latest <= CyclesPerSecond ? PacketsPerSecond : 0.0f;
		}

	private:
		uint32 Earliest = 0;
		uint32 Latest = 0;
		uint32 NumPackets = 0;
		float PacketsPerSecond = 0.0f;
		const double CyclesPerSecond = 1.0 / FPlatformTime::GetSecondsPerCycle();
	};
}

#endif
