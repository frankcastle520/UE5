// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "EpicRtcVideoCommon.h"
#include "IPixelStreaming2Streamer.h"
#include "PixelStreaming2PluginSettings.h"
#include "Video/VideoEncoder.h"

#include "epic_rtc/core/video/video_common.h"
#include "epic_rtc/core/video/video_rate_control.h"

namespace UE::PixelStreaming2
{
	// HACK (aidan.possemiers) the AVCodecs API surface wants a SharedPtr for the encoded data but EpicRtc already owns that and we don't want AVCodecs to delete it
	struct FFakeDeleter
	{
		void operator()(uint8* Object) const
		{
		}
	};

	// List of video codecs supported by PS2
	const TStaticArray<EVideoCodec, 4> SupportedVideoCodecs = { EVideoCodec::H264, EVideoCodec::AV1, EVideoCodec::VP8, EVideoCodec::VP9 };

	constexpr uint32_t NumSimulcastLayers = 3;
	// Each subsequent layer is 1/ScalingFactor the size of the previous
	constexpr uint32_t ScalingFactor = 2;

	// Helper array for all scalability modes. EScalabilityMode::None must always be the last entry
	const TArray<EScalabilityMode> AllScalabilityModes = {
		EScalabilityMode::L1T1,
		EScalabilityMode::L1T2,
		EScalabilityMode::L1T3,
		EScalabilityMode::L2T1,
		EScalabilityMode::L2T1h,
		EScalabilityMode::L2T1_KEY,
		EScalabilityMode::L2T2,
		EScalabilityMode::L2T2h,
		EScalabilityMode::L2T2_KEY,
		EScalabilityMode::L2T2_KEY_SHIFT,
		EScalabilityMode::L2T3,
		EScalabilityMode::L2T3h,
		EScalabilityMode::L2T3_KEY,
		EScalabilityMode::L3T1,
		EScalabilityMode::L3T1h,
		EScalabilityMode::L3T1_KEY,
		EScalabilityMode::L3T2,
		EScalabilityMode::L3T2h,
		EScalabilityMode::L3T2_KEY,
		EScalabilityMode::L3T3,
		EScalabilityMode::L3T3h,
		EScalabilityMode::L3T3_KEY,
		EScalabilityMode::S2T1,
		EScalabilityMode::S2T1h,
		EScalabilityMode::S2T2,
		EScalabilityMode::S2T2h,
		EScalabilityMode::S2T3,
		EScalabilityMode::S2T3h,
		EScalabilityMode::S3T1,
		EScalabilityMode::S3T1h,
		EScalabilityMode::S3T2,
		EScalabilityMode::S3T2h,
		EScalabilityMode::S3T3,
		EScalabilityMode::S3T3h,
		EScalabilityMode::None
	};

	// Make sure EpicRtcVideoScalabilityMode and EScalabilityMode match up
	static_assert(EpicRtcVideoScalabilityMode::L1T1 == static_cast<EpicRtcVideoScalabilityMode>(EScalabilityMode::L1T1));
	static_assert(EpicRtcVideoScalabilityMode::L1T2 == static_cast<EpicRtcVideoScalabilityMode>(EScalabilityMode::L1T2));
	static_assert(EpicRtcVideoScalabilityMode::L1T3 == static_cast<EpicRtcVideoScalabilityMode>(EScalabilityMode::L1T3));
	static_assert(EpicRtcVideoScalabilityMode::L2T1 == static_cast<EpicRtcVideoScalabilityMode>(EScalabilityMode::L2T1));
	static_assert(EpicRtcVideoScalabilityMode::L2T1h == static_cast<EpicRtcVideoScalabilityMode>(EScalabilityMode::L2T1h));
	static_assert(EpicRtcVideoScalabilityMode::L2T1Key == static_cast<EpicRtcVideoScalabilityMode>(EScalabilityMode::L2T1_KEY));
	static_assert(EpicRtcVideoScalabilityMode::L2T2 == static_cast<EpicRtcVideoScalabilityMode>(EScalabilityMode::L2T2));
	static_assert(EpicRtcVideoScalabilityMode::L2T2h == static_cast<EpicRtcVideoScalabilityMode>(EScalabilityMode::L2T2h));
	static_assert(EpicRtcVideoScalabilityMode::L2T2Key == static_cast<EpicRtcVideoScalabilityMode>(EScalabilityMode::L2T2_KEY));
	static_assert(EpicRtcVideoScalabilityMode::L2T2KeyShift == static_cast<EpicRtcVideoScalabilityMode>(EScalabilityMode::L2T2_KEY_SHIFT));
	static_assert(EpicRtcVideoScalabilityMode::L2T3 == static_cast<EpicRtcVideoScalabilityMode>(EScalabilityMode::L2T3));
	static_assert(EpicRtcVideoScalabilityMode::L2T3h == static_cast<EpicRtcVideoScalabilityMode>(EScalabilityMode::L2T3h));
	static_assert(EpicRtcVideoScalabilityMode::L2T3Key == static_cast<EpicRtcVideoScalabilityMode>(EScalabilityMode::L2T3_KEY));
	static_assert(EpicRtcVideoScalabilityMode::L3T1 == static_cast<EpicRtcVideoScalabilityMode>(EScalabilityMode::L3T1));
	static_assert(EpicRtcVideoScalabilityMode::L3T1h == static_cast<EpicRtcVideoScalabilityMode>(EScalabilityMode::L3T1h));
	static_assert(EpicRtcVideoScalabilityMode::L3T1Key == static_cast<EpicRtcVideoScalabilityMode>(EScalabilityMode::L3T1_KEY));
	static_assert(EpicRtcVideoScalabilityMode::L3T2 == static_cast<EpicRtcVideoScalabilityMode>(EScalabilityMode::L3T2));
	static_assert(EpicRtcVideoScalabilityMode::L3T2h == static_cast<EpicRtcVideoScalabilityMode>(EScalabilityMode::L3T2h));
	static_assert(EpicRtcVideoScalabilityMode::L3T2Key == static_cast<EpicRtcVideoScalabilityMode>(EScalabilityMode::L3T2_KEY));
	static_assert(EpicRtcVideoScalabilityMode::L3T3 == static_cast<EpicRtcVideoScalabilityMode>(EScalabilityMode::L3T3));
	static_assert(EpicRtcVideoScalabilityMode::L3T3h == static_cast<EpicRtcVideoScalabilityMode>(EScalabilityMode::L3T3h));
	static_assert(EpicRtcVideoScalabilityMode::L3T3Key == static_cast<EpicRtcVideoScalabilityMode>(EScalabilityMode::L3T3_KEY));
	static_assert(EpicRtcVideoScalabilityMode::S2T1 == static_cast<EpicRtcVideoScalabilityMode>(EScalabilityMode::S2T1));
	static_assert(EpicRtcVideoScalabilityMode::S2T1h == static_cast<EpicRtcVideoScalabilityMode>(EScalabilityMode::S2T1h));
	static_assert(EpicRtcVideoScalabilityMode::S2T2 == static_cast<EpicRtcVideoScalabilityMode>(EScalabilityMode::S2T2));
	static_assert(EpicRtcVideoScalabilityMode::S2T2h == static_cast<EpicRtcVideoScalabilityMode>(EScalabilityMode::S2T2h));
	static_assert(EpicRtcVideoScalabilityMode::S2T3 == static_cast<EpicRtcVideoScalabilityMode>(EScalabilityMode::S2T3));
	static_assert(EpicRtcVideoScalabilityMode::S2T3h == static_cast<EpicRtcVideoScalabilityMode>(EScalabilityMode::S2T3h));
	static_assert(EpicRtcVideoScalabilityMode::S3T1 == static_cast<EpicRtcVideoScalabilityMode>(EScalabilityMode::S3T1));
	static_assert(EpicRtcVideoScalabilityMode::S3T1h == static_cast<EpicRtcVideoScalabilityMode>(EScalabilityMode::S3T1h));
	static_assert(EpicRtcVideoScalabilityMode::S3T2 == static_cast<EpicRtcVideoScalabilityMode>(EScalabilityMode::S3T2));
	static_assert(EpicRtcVideoScalabilityMode::S3T2h == static_cast<EpicRtcVideoScalabilityMode>(EScalabilityMode::S3T2h));
	static_assert(EpicRtcVideoScalabilityMode::S3T3 == static_cast<EpicRtcVideoScalabilityMode>(EScalabilityMode::S3T3));
	static_assert(EpicRtcVideoScalabilityMode::S3T3h == static_cast<EpicRtcVideoScalabilityMode>(EScalabilityMode::S3T3h));
	static_assert(EpicRtcVideoScalabilityMode::None == static_cast<EpicRtcVideoScalabilityMode>(EScalabilityMode::None));

	/**
	 * A struct representing the simulcast paramaters of a single simulcast layer used by PixelStreaming2.
	 * Specifically, each layer has a `Scaling`, `MinBitrate` and `MaxBitrate`.
	 */
	struct FPixelStreaming2SimulcastLayer
	{
		float Scaling;
		int	  MinBitrate;
		int	  MaxBitrate;
	};

	inline TArray<FPixelStreaming2SimulcastLayer> GetSimulcastParameters()
	{
		TArray<FPixelStreaming2SimulcastLayer> SimulcastParams;

		if (UPixelStreaming2PluginSettings::CVarEncoderEnableSimulcast.GetValueOnAnyThread())
		{
			int MinBps = UPixelStreaming2PluginSettings::CVarWebRTCMinBitrate.GetValueOnAnyThread();
			int MaxBps = UPixelStreaming2PluginSettings::CVarWebRTCMaxBitrate.GetValueOnAnyThread();
			int BpsRange = MaxBps - MinBps;

			// Bitrates assignment per layer. Effectively
			// 0: 0 -> 1/4
			// 1: 1/4 -> 1/2
			// 2: 1/2 -> 1
			TArray<TTuple<int, int>> Bitrates = {
				{ MinBps, MinBps + (BpsRange / 4) },
				{ MinBps + (BpsRange / 4), MinBps + (BpsRange / 2) },
				{ MinBps + (BpsRange / 2), MaxBps }
			};

			for (int i = 0; i < NumSimulcastLayers; i++)
			{
				// EpicRtc expects the layers to be added in order of scaling factors from largest to smallest (ie smallest res to largest res)
				float Scaling = ScalingFactor * (NumSimulcastLayers - i - 1);

				// clang-format off
				SimulcastParams.Add({
					.Scaling = Scaling > 0 ? Scaling : 1.f,
					.MinBitrate = Bitrates[i].Get<0>(),
					.MaxBitrate = Bitrates[i].Get<1>(),
				});
				// clang-format on
			}
		}
		else
		{
			// clang-format off
			SimulcastParams.Add({ 
				.Scaling = 1.f,
				.MinBitrate = UPixelStreaming2PluginSettings::CVarWebRTCMinBitrate.GetValueOnAnyThread(),
				.MaxBitrate = UPixelStreaming2PluginSettings::CVarWebRTCMaxBitrate.GetValueOnAnyThread() 
			});
			// clang-format on
		}

		return SimulcastParams;
	}

	inline FEpicRtcParameterPairArray* CreateH264Format(EH264Profile Profile, UE::AVCodecCore::H264::EH264Level Level)
	{
		// TODO (Migration): RTCP-7028 picRtcStringView needs a way to own the memory passed into it
		// return new FEpicRtcParameterPairArray(
		// {
		// 	EpicRtcParameterPair{
		// 		._key = EpicRtcStringView{ ._ptr = "profile-level-id", ._length = 16 },
		// 		._value = EpicRtcStringView{ ._ptr = TCHAR_TO_ANSI(*ProfileString), ._length = (uint64_t)ProfileString.Len() }
		// 	},
		// 	EpicRtcParameterPair{
		// 		._key = EpicRtcStringView{ ._ptr = "packetization-mode", ._length = 18 },
		// 		._value = EpicRtcStringView{ ._ptr = "1", ._length = 1 }
		// 	},
		// 	EpicRtcParameterPair{
		// 		._key = EpicRtcStringView{ ._ptr = "level-asymmetry-allowed", ._length = 23 },
		// 		._value = EpicRtcStringView{ ._ptr = "1", ._length = 1 }
		// 	}
		// });

		using namespace UE::AVCodecCore::H264;
		if (Profile == EH264Profile::ConstrainedBaseline && Level == EH264Level::Level_3_1)
		{
			return new FEpicRtcParameterPairArray(
				{ EpicRtcParameterPair{
					  ._key = EpicRtcStringView{ ._ptr = "profile-level-id", ._length = 16 },
					  ._value = EpicRtcStringView{ ._ptr = "42e01f", ._length = 6 } },
					EpicRtcParameterPair{
						._key = EpicRtcStringView{ ._ptr = "packetization-mode", ._length = 18 },
						._value = EpicRtcStringView{ ._ptr = "1", ._length = 1 } },
					EpicRtcParameterPair{
						._key = EpicRtcStringView{ ._ptr = "level-asymmetry-allowed", ._length = 23 },
						._value = EpicRtcStringView{ ._ptr = "1", ._length = 1 } } });
		}
		else if (Profile == EH264Profile::Baseline && Level == EH264Level::Level_3_1)
		{
			return new FEpicRtcParameterPairArray(
				{ EpicRtcParameterPair{
					  ._key = EpicRtcStringView{ ._ptr = "profile-level-id", ._length = 16 },
					  ._value = EpicRtcStringView{ ._ptr = "42001f", ._length = 6 } },
					EpicRtcParameterPair{
						._key = EpicRtcStringView{ ._ptr = "packetization-mode", ._length = 18 },
						._value = EpicRtcStringView{ ._ptr = "1", ._length = 1 } },
					EpicRtcParameterPair{
						._key = EpicRtcStringView{ ._ptr = "level-asymmetry-allowed", ._length = 23 },
						._value = EpicRtcStringView{ ._ptr = "1", ._length = 1 } } });
		}
		else
		{
			return nullptr;
		}
	}

	inline FEpicRtcParameterPairArray* CreateVP9Format(UE::AVCodecCore::VP9::EProfile Profile)
	{
		using namespace UE::AVCodecCore::VP9;
		if (Profile == EProfile::Profile0)
		{
			return new FEpicRtcParameterPairArray(
				{ EpicRtcParameterPair{
					._key = EpicRtcStringView{ ._ptr = "profile-id", ._length = 10 },
					._value = EpicRtcStringView{ ._ptr = "0", ._length = 1 } } });
		}
		else if (Profile == EProfile::Profile1)
		{
			return new FEpicRtcParameterPairArray(
				{ EpicRtcParameterPair{
					._key = EpicRtcStringView{ ._ptr = "profile-id", ._length = 10 },
					._value = EpicRtcStringView{ ._ptr = "1", ._length = 1 } } });
		}
		else if (Profile == EProfile::Profile2)
		{
			return new FEpicRtcParameterPairArray(
				{ EpicRtcParameterPair{
					._key = EpicRtcStringView{ ._ptr = "profile-id", ._length = 10 },
					._value = EpicRtcStringView{ ._ptr = "2", ._length = 1 } } });
		}
		else if (Profile == EProfile::Profile3)
		{
			return new FEpicRtcParameterPairArray(
				{ EpicRtcParameterPair{
					._key = EpicRtcStringView{ ._ptr = "profile-id", ._length = 10 },
					._value = EpicRtcStringView{ ._ptr = "3", ._length = 1 } } });
		}
		else
		{
			return nullptr;
		}
	}
} // namespace UE::PixelStreaming2