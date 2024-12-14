// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlayerCore.h"
#include "StreamTypes.h"
#include "Utilities/StringHelpers.h"
#include "Utilities/Utilities.h"
#include "ElectraDecodersUtils.h"

namespace Electra
{

	namespace
	{
		static constexpr uint32 Make4CC(const uint8 A, const uint8 B, const uint8 C, const uint8 D)
		{
			return (static_cast<uint32>(A) << 24) | (static_cast<uint32>(B) << 16) | (static_cast<uint32>(C) << 8) | static_cast<uint32>(D);
		}
		static constexpr uint32 Make4CC(const uint32 FourCC)
		{
			return Make4CC((FourCC >> 24) & 255, (FourCC >> 16) & 255, (FourCC >> 8) & 255, FourCC & 255);
		}
		FString Printable4CC(const uint32 In4CC)
		{
			FString Out;
			// Not so much just printable as alphanumeric.
			for(uint32 i=0, Atom=In4CC; i<4; ++i, Atom<<=8)
			{
				int32 v = Atom >> 24;
				if ((v >= 'A' && v <= 'Z') || (v >= 'a' && v <= 'z') || (v >= '0' && v <= '9') || v == '_'|| v == '.')
				{
					Out.AppendChar(v);
				}
				else
				{
					// Not alphanumeric, return it as a hex string.
					return FString::Printf(TEXT("%08x"), In4CC);
				}
			}
			return Out;
		}
	}


	FString FStreamCodecInformation::GetMimeType() const
	{
		if (!MimeType.IsEmpty())
		{
			return MimeType;
		}
		switch(GetCodec())
		{
			case ECodec::H264:
				return FString(TEXT("video/mp4"));
			case ECodec::H265:
				return FString(TEXT("video/mp4"));
			case ECodec::AAC:
				return FString(TEXT("audio/mp4"));
			case ECodec::EAC3:
				return FString(TEXT("audio/mp4"));
			case ECodec::WebVTT:
			case ECodec::TTML:
			case ECodec::TX3G:
			case ECodec::OtherSubtitle:
				return FString(TEXT("application/mp4"));
			default:
				return FString(TEXT("application/octet-stream"));
		}
	}

	FString FStreamCodecInformation::GetMimeTypeWithCodec() const
	{
		return GetMimeType() + FString::Printf(TEXT("; codecs=\"%s\""), *GetCodecSpecifierRFC6381());
	}

	FString FStreamCodecInformation::GetMimeTypeWithCodecAndFeatures() const
	{
		if (GetStreamType() == EStreamType::Video && GetResolution().Width && GetResolution().Height)
		{
			return GetMimeTypeWithCodec() + FString::Printf(TEXT("; resolution=%dx%d"), GetResolution().Width, GetResolution().Height);
		}
		return GetMimeTypeWithCodec();
	}


	bool FStreamCodecInformation::ParseFromRFC6381(const FString& CodecOTI)
	{
		if (CodecOTI.StartsWith(TEXT("avc")))
		{
			// avc1 and avc3 (inband SPS/PPS) are recognized.
			StreamType = EStreamType::Video;
			CodecSpecifier = CodecOTI;
			Codec = ECodec::H264;
			if (CodecOTI.Len() > 3)
			{
				// avc 1 or 3 only.
				if (CodecOTI[3] != TCHAR('1') && CodecOTI[3] != TCHAR('3'))
				{
					return false;
				}
				// Profile and level follow?
				if (CodecOTI.Len() > 5 && CodecOTI[4] == TCHAR('.'))
				{
					int32 DotPos;
					CodecOTI.FindLastChar(TCHAR('.'), DotPos);

					FString Temp;
					int32 TempValue;
					// We recognize the expected format avcC.xxyyzz and for legacy reasons also avcC.xxx.zz
					if (CodecOTI.Len() == 11 && DotPos == 4)
					{
						Temp = CodecOTI.Mid(5, 2);
						LexFromStringHex(TempValue, *Temp);
						SetProfile(TempValue);
						Temp = CodecOTI.Mid(7, 2);
						LexFromStringHex(TempValue, *Temp);
						SetProfileConstraints(TempValue);
						Temp = CodecOTI.Mid(9, 2);
						LexFromStringHex(TempValue, *Temp);
						SetProfileLevel(TempValue);
					}
					else if (DotPos != INDEX_NONE)
					{
						Temp = CodecOTI.Mid(5, DotPos-5);
						LexFromString(TempValue, *Temp);
						SetProfile(TempValue);
						Temp = CodecOTI.Mid(DotPos+1);
						LexFromString(TempValue, *Temp);
						SetProfileLevel(TempValue);
						// Change the string to the expected format.
						SetCodecSpecifierRFC6381(FString::Printf(TEXT("avc%c.%02x00%02x"), CodecOTI[3], GetProfile(), GetProfileLevel()));
					}
					else
					{
						return false;
					}
				}
			}
			return true;
		}
		else if (CodecOTI.StartsWith(TEXT("hvc")) || CodecOTI.StartsWith(TEXT("hev")))
		{
			FString oti = CodecOTI;
			FString Temp;
			// hvc1 and hev1 (inband VPS/SPS/PPS) are recognized.
			StreamType = EStreamType::Video;
			CodecSpecifier = CodecOTI;
			Codec = ECodec::H265;

			int32 DotPos;
			if (oti.FindChar(TCHAR('.'), DotPos))
			{
				int32 general_profile_space = 0;
				int32 general_tier_flag = 0;
				int32 general_profile_idc = 0;
				int32 general_level_idc = 0;
				uint32 general_profile_compatibility_flag = 0;
				uint64 contraint_flags = 0;
				oti.RightChopInline(DotPos + 1);
				// optional general_profile_space
				if (oti[0] == TCHAR('A') || oti[0] == TCHAR('B') || oti[0] == TCHAR('C'))
				{
					general_profile_space = oti[0] - TCHAR('A') + 1;
					oti.RightChopInline(1);
				}
				else if (oti[0] == TCHAR('a') || oti[0] == TCHAR('b') || oti[0] == TCHAR('c'))
				{
					general_profile_space = oti[0] - TCHAR('a') + 1;
					oti.RightChopInline(1);
				}
				// general_profile_idc
				if (oti.FindChar(TCHAR('.'), DotPos))
				{
					Temp = oti.Left(DotPos);
					oti.RightChopInline(DotPos + 1);
					LexFromString(general_profile_idc, *Temp);
				}
				// general_profile_compatibility_flags
				if (oti.FindChar(TCHAR('.'), DotPos))
				{
					Temp = oti.Left(DotPos);
					oti.RightChopInline(DotPos + 1);
					LexFromString(general_profile_compatibility_flag, *Temp);
				}
				// general_tier_flag
				if (oti[0] != TCHAR('L') && oti[0] != TCHAR('H') && oti[0] != TCHAR('l') && oti[0] != TCHAR('h'))
				{
					return false;
				}
				else if (oti[0] == TCHAR('H') || oti[0] == TCHAR('h'))
				{
					general_tier_flag = 1;
				}
				oti.RightChopInline(1);
				// constraint_flags
				FString ConstraintFlags;
				if (oti.FindChar(TCHAR('.'), DotPos))
				{
					ConstraintFlags = oti.Mid(DotPos + 1);
					oti.LeftInline(DotPos);
					ConstraintFlags.ReplaceInline(TEXT("."), TEXT(""));
					ConstraintFlags += TEXT("000000000000");
					ConstraintFlags.LeftInline(12);
					LexFromStringHexU64(contraint_flags, *ConstraintFlags);
				}
				// general_level_idc
				LexFromString(general_level_idc, *oti);
				SetProfileSpace(general_profile_space);
				SetProfileCompatibilityFlags(Utils::BitReverse32(general_profile_compatibility_flag));
				SetProfileTier(general_tier_flag);
				SetProfile(general_profile_idc);
				SetProfileLevel(general_level_idc);
				SetProfileConstraints(contraint_flags);
				return true;
			}
			return false;
		}
		else if (CodecOTI.StartsWith(TEXT("dvh1")) || CodecOTI.StartsWith(TEXT("dvhe")))
		{
			// Dolby Vision only recognized as a generic Video 4CC for now.
			StreamType = EStreamType::Video;
			CodecSpecifier = CodecOTI;
			Codec = ECodec::Video4CC;
			Codec4CC = CodecOTI[3] == TCHAR('1') ? Make4CC('d','v','h','1') : Make4CC('d','v','h','e');
			return true;
		}
		else if (CodecOTI.StartsWith(TEXT("mp4a")))
		{
			StreamType = EStreamType::Audio;
			CodecSpecifier = CodecOTI;
			Codec = ECodec::AAC;
			// Object and profile follow?
			if (CodecOTI.Len() > 6 && CodecOTI[4] == TCHAR('.'))
			{
				// mp4a.40.d and mp4a.6b are recognized.
				FString OT, Profile;
				int32 DotPos = CodecOTI.Find(TEXT("."), ESearchCase::CaseSensitive, ESearchDir::FromStart, 5);
				OT = CodecOTI.Mid(5, DotPos != INDEX_NONE ? DotPos - 5 : DotPos);
				Profile = CodecOTI.Mid(DotPos != INDEX_NONE ? DotPos + 1 : DotPos);
				if (OT.Equals(TEXT("40")))
				{
					int32 ProfileValue = 0;
					LexFromString(ProfileValue, *Profile);
					SetProfile(ProfileValue);
					// AAC-LC, AAC-HE (SBR), AAC-HEv2 (PS), MP3
					if (!(ProfileValue == 2 || ProfileValue == 5 || ProfileValue == 29 || ProfileValue == 34))
					{
						return false;
					}
					if (ProfileValue == 34)
					{
						Codec = ECodec::Audio4CC;
						Codec4CC = Make4CC('m','p','g','a');
						MimeType = TEXT("audio/mpeg");
						SetProfile(1);
						SetProfileLevel(3);
					}
				}
				else if (OT.Equals(TEXT("6b"), ESearchCase::IgnoreCase))
				{
					Codec = ECodec::Audio4CC;
					Codec4CC = Make4CC('m','p','g','a');
					MimeType = TEXT("audio/mpeg");
					SetProfile(1);
					SetProfileLevel(3);
				}
				else
				{
					return false;
				}
			}
			return true;
		}
		else if (CodecOTI.StartsWith(TEXT("ec-3")) || CodecOTI.StartsWith(TEXT("ec+3")) || CodecOTI.StartsWith(TEXT("ec3")) || CodecOTI.StartsWith(TEXT("eac3")))
		{
			StreamType = EStreamType::Audio;
			CodecSpecifier = CodecOTI;
			Codec = ECodec::EAC3;
			// Presently not supported.
			return false;
		}
		else if (CodecOTI.StartsWith(TEXT("ac-3")) || CodecOTI.StartsWith(TEXT("ac3")))
		{
			StreamType = EStreamType::Audio;
			CodecSpecifier = CodecOTI;
			Codec = ECodec::AC3;
			// Presently not supported.
			return false;
		}
		else if (CodecOTI.Equals(TEXT("opus"), ESearchCase::IgnoreCase))
		{
			StreamType = EStreamType::Audio;
			CodecSpecifier = CodecOTI;
			Codec = ECodec::Audio4CC;
			Codec4CC = Make4CC('O','p','u','s');
			return true;
		}
		else if (CodecOTI.StartsWith(TEXT("vp08")))
		{
			ElectraDecodersUtil::FMimeTypeVideoCodecInfo ci;
			if (ElectraDecodersUtil::ParseCodecVP8(ci, CodecOTI, Extras.GetValue(StreamCodecInformationOptions::VPccBox).SafeGetArray()))
			{
				StreamType = EStreamType::Video;
				CodecSpecifier = CodecOTI;
				Codec = ECodec::Video4CC;
				Codec4CC = Make4CC('v','p','0','8');
				SetProfile(ci.Profile);
				SetProfileLevel(ci.Level);
				SetCodecSpecifierRFC6381(FString::Printf(TEXT("vp08.%02d.%02d.%02d"), ci.Profile, ci.Level, ci.NumBitsLuma));
				return true;
			}
			return false;
		}
		else if (CodecOTI.Equals(TEXT("vp8")))
		{
			StreamType = EStreamType::Video;
			CodecSpecifier = CodecOTI;
			Codec = ECodec::Video4CC;
			Codec4CC = Make4CC('v','p','0','8');
			SetCodecSpecifierRFC6381(FString::Printf(TEXT("vp08.%02d.%02d.%02d"), 0, 0, 8));
			return true;
		}
		else if (CodecOTI.StartsWith(TEXT("vp09")))
		{
			ElectraDecodersUtil::FMimeTypeVideoCodecInfo ci;
			if (ElectraDecodersUtil::ParseCodecVP9(ci, CodecOTI, Extras.GetValue(StreamCodecInformationOptions::VPccBox).SafeGetArray()))
			{
				StreamType = EStreamType::Video;
				CodecSpecifier = CodecOTI;
				Codec = ECodec::Video4CC;
				Codec4CC = Make4CC('v','p','0','9');
				SetProfile(ci.Profile);
				SetProfileLevel(ci.Level);
				SetCodecSpecifierRFC6381(FString::Printf(TEXT("vp09.%02d.%02d.%02d.%02d.%02d.%02d.%02d.%02d"), ci.Profile, ci.Level, ci.NumBitsLuma, ci.Extras[3], ci.Extras[4], ci.Extras[5], ci.Extras[6], ci.Extras[7]));
				return true;
			}
			return false;
		}
		else if (CodecOTI.Equals(TEXT("vp9")))
		{
			StreamType = EStreamType::Video;
			CodecSpecifier = CodecOTI;
			Codec = ECodec::Video4CC;
			Codec4CC = Make4CC('v','p','0','9');
			SetCodecSpecifierRFC6381(FString::Printf(TEXT("vp09.%02d.%02d.%02d"), 0, 0, 8));
			return true;
		}
		else if (CodecOTI.Equals(TEXT("wvtt")))
		{
			StreamType = EStreamType::Subtitle;
			CodecSpecifier = CodecOTI;
			Codec = ECodec::WebVTT;
			return true;
		}
		// This is indicating one of the many TTML variants (eg. IMSC1, SMPTE-TT, EBU-TT) and profiles (eg. stpp.ttml.im1t)
		//	See: https://www.w3.org/TR/ttml-profile-registry/#registry-profile-designator-specifications
		else if (CodecOTI.StartsWith(TEXT("stpp")))
		{
			StreamType = EStreamType::Subtitle;
			CodecSpecifier = CodecOTI;
			Codec = ECodec::TTML;
			return true;
		}
		else if (CodecOTI.Equals(TEXT("tx3g")))
		{
			StreamType = EStreamType::Subtitle;
			CodecSpecifier = CodecOTI;
			Codec = ECodec::TX3G;
			return true;
		}
		else
		{
			StreamType = EStreamType::Unsupported;
			CodecSpecifier = CodecOTI;
			Codec = ECodec::Unknown;
			return false;
		}
	}

	FString FStreamCodecInformation::GetCodecName() const
	{
		switch(Codec)
		{
			case FStreamCodecInformation::ECodec::H264:
				return FString(TEXT("avc"));
			case FStreamCodecInformation::ECodec::H265:
				return FString(TEXT("hevc"));
			case FStreamCodecInformation::ECodec::AAC:
				return FString(TEXT("aac"));
			case FStreamCodecInformation::ECodec::EAC3:
				return FString(TEXT("eac3"));
			case FStreamCodecInformation::ECodec::WebVTT:
				return FString(TEXT("wvtt"));
			case FStreamCodecInformation::ECodec::TTML:
				return FString(TEXT("stpp"));
			case FStreamCodecInformation::ECodec::TX3G:
				return FString(TEXT("tx3g"));
			case FStreamCodecInformation::ECodec::OtherSubtitle:
				return FString(TEXT("subt"));
			case FStreamCodecInformation::ECodec::Video4CC:
			case FStreamCodecInformation::ECodec::Audio4CC:
				return Printable4CC(Codec4CC);
			default:
				return FString(TEXT("unknown"));
		}
	}

	const FString& FStreamCodecInformation::GetHumanReadableCodecName() const
	{
		if (HumanReadableCodecName.IsEmpty())
		{
			if (!TryConstructHumanReadableCodecName())
			{
				HumanReadableCodecName = CodecSpecifier;
			}
		}
		return HumanReadableCodecName;
	}

	bool FStreamCodecInformation::TryConstructHumanReadableCodecName() const
	{
		switch(GetCodec())
		{
			case ECodec::H264:
			{
				HumanReadableCodecName = TEXT("AVC (H.264)");
				if (ProfileLevel.Profile == 66)
				{
					HumanReadableCodecName.Append(TEXT(", Baseline"));
				}
				else if (ProfileLevel.Profile == 77)
				{
					HumanReadableCodecName.Append(TEXT(", Main"));
				}
				else if (ProfileLevel.Profile == 100)
				{
					HumanReadableCodecName.Append(TEXT(", High"));
				}
				else
				{
					HumanReadableCodecName.Append(TEXT(", Unknown profile"));
				}
				HumanReadableCodecName.Append(FString::Printf(TEXT(", level %d.%d"), ProfileLevel.Level/10, ProfileLevel.Level%10));
				return true;
			}
			case ECodec::H265:
			{
				HumanReadableCodecName = TEXT("HEVC (H.265)");
				if (ProfileLevel.Profile == 1)
				{
					HumanReadableCodecName.Append(TEXT(", Main"));
				}
				else if (ProfileLevel.Profile == 2)
				{
					HumanReadableCodecName.Append(TEXT(", Main10"));
				}
				else
				{
					HumanReadableCodecName.Append(TEXT(", Unknown profile"));
				}
				HumanReadableCodecName.Append(FString::Printf(TEXT(", level %d.%d"), ProfileLevel.Level/30, ProfileLevel.Level%30));
				return true;
			}
			case ECodec::Video4CC:
			{
				switch(GetCodec4CC())
				{
					case Make4CC('v','p','0','8'):
						HumanReadableCodecName = TEXT("VP8");
						return true;
					case Make4CC('v','p','0','9'):
						HumanReadableCodecName = TEXT("VP9");
						return true;

					case Make4CC('a','p','c','h'):
						HumanReadableCodecName = TEXT("Apple ProRes 422 High Quality");
						return true;
					case Make4CC('a','p','c','n'):
						HumanReadableCodecName = TEXT("Apple ProRes 422 Standard Definition");
						return true;
					case Make4CC('a','p','c','s'):
						HumanReadableCodecName = TEXT("Apple ProRes 422 LT");
						return true;
					case Make4CC('a','p','c','o'):
						HumanReadableCodecName = TEXT("Apple ProRes 422 Proxy");
						return true;
					case Make4CC('a','p','4','h'):
						HumanReadableCodecName = TEXT("Apple ProRes 4444");
						return true;

					case Make4CC('H','a','p','1'):
						HumanReadableCodecName = TEXT("Hap");
						return true;
					case Make4CC('H','a','p','5'):
						HumanReadableCodecName = TEXT("Hap Alpha");
						return true;
					case Make4CC('H','a','p','Y'):
						HumanReadableCodecName = TEXT("Hap Q");
						return true;
					case Make4CC('H','a','p','M'):
						HumanReadableCodecName = TEXT("Hap Q Alpha");
						return true;
					case Make4CC('H','a','p','7'):
						HumanReadableCodecName = TEXT("Hap R");
						return true;
					case Make4CC('H','a','p','H'):
						HumanReadableCodecName = TEXT("Hap HDR");
						return true;

					case Make4CC('A','V','d','h'):
						HumanReadableCodecName = TEXT("Avid DNxHD");
						return true;
				}
				HumanReadableCodecName = Printable4CC(GetCodec4CC());
				return true;
			}
			case ECodec::AAC:
			{
				HumanReadableCodecName = TEXT("MPEG AAC");
				return true;
			}
			case ECodec::EAC3:
			{
				HumanReadableCodecName = TEXT("Dolby Digital");
				return true;
			}
			case ECodec::Audio4CC:
			{
				switch(GetCodec4CC())
				{
					case Make4CC('O','p','u','s'):
					{
						HumanReadableCodecName = TEXT("Opus");
						return true;
					}
					case Make4CC('f','L','a','C'):
					{
						HumanReadableCodecName = TEXT("Free Lossless Audio Codec (FLAC)");
						return true;
					}
					case Make4CC('m','p','g','a'):
					{
						if (GetProfileLevel())
						{
							HumanReadableCodecName = FString::Printf(TEXT("MPEG%d Layer %d"), GetProfile(), GetProfileLevel());
						}
						else
						{
							HumanReadableCodecName = FString::Printf(TEXT("MPEG%d audio"), GetProfile());
						}
						return true;
					}
				}
				HumanReadableCodecName = Printable4CC(GetCodec4CC());
				return true;
			}
			case ECodec::WebVTT:
			{
				HumanReadableCodecName = TEXT("WebVTT");
				return true;
			}
			case ECodec::TTML:
			{
				HumanReadableCodecName = TEXT("TTML");
				return true;
			}
			case ECodec::TX3G:
			{
				HumanReadableCodecName = TEXT("SRT/TX3G");
				return true;
			}
		}
		return false;
	}




	bool FCodecSelectionPriorities::Initialize(const FString& ConfigurationString)
	{
		ClassPriorities.Empty();
		if (ConfigurationString.Len() && !ParseInternal(ConfigurationString))
		{
			ClassPriorities.Empty();
			return false;
		}
		return true;
	}
	bool FCodecSelectionPriorities::ParseInternal(const FString& ConfigurationString)
	{
		auto SkipWhiteSpaces = [](StringHelpers::FStringIterator& it) -> void
		{
			while(it && TChar<TCHAR>::IsWhitespace(*it))
			{
				++it;
			}
		};

		auto ParsePriority = [](int32& OutPrio, StringHelpers::FStringIterator& it, bool bInClass) -> bool
		{
			int64 Prio = 0;
			bool bEmpty = true;
			while(it && TChar<TCHAR>::IsDigit(*it))
			{
				bEmpty = false;
				Prio *= 10;
				Prio += *it - TCHAR('0');
				++it;
			}
			while(it && TChar<TCHAR>::IsWhitespace(*it))
			{
				++it;
			}
			// Did we end the priority properly?
			if (!bEmpty && (!it || *it == TCHAR(',') || (bInClass && *it == TCHAR('{'))))
			{
				OutPrio = Prio;
				return true;
			}
			// Unexpected next character. Fail!
			return false;
		};

		const TCHAR* const CommaDelimiter = TEXT(",");
		StringHelpers::FStringIterator it(ConfigurationString);
		while(it)
		{
			FClassPriority ClassPriority;
			SkipWhiteSpaces(it);
			while(it && *it != TCHAR('=') && *it != TCHAR('{') && *it != TCHAR(','))
			{
				ClassPriority.Prefix += *it++;
			}
			if (ClassPriority.Prefix.Len() == 0)
			{
				return false;
			}

			// Is the next char assigning a priority?
			if (it && *it == TCHAR('='))
			{
				// Get the class priority
				++it;
				if (!ParsePriority(ClassPriority.Priority, it, true))
				{
					return false;
				}
			}
			// If no priority then there must now be a group for stream specific priorities.
			else if (!it || *it != TCHAR('{'))
			{
				return false;
			}
			// Do stream specific priorities follow?
			if (it && *it == TCHAR('{'))
			{
				int32 GroupStart = it.GetIndex();
				// Look for the end of the group.
				while(it && *it != TCHAR('}'))
				{
					++it;
				}
				if (!it || *it != TCHAR('}'))
				{
					return false;
				}
				++it;
				FString Group = ConfigurationString.Mid(GroupStart+1, it.GetIndex()-GroupStart-2);
				TArray<FString> StreamPriorities;
				Group.ParseIntoArray(StreamPriorities, CommaDelimiter, true);
				if (StreamPriorities.Num() == 0)
				{
					return false;
				}
				for(auto &sp : StreamPriorities)
				{
					FStreamPriority StreamPriority;
					StringHelpers::FStringIterator spIt(sp);
					while(spIt)
					{
						SkipWhiteSpaces(spIt);
						while(spIt && *spIt != TCHAR('=') && *spIt != TCHAR('{') && *spIt != TCHAR(','))
						{
							StreamPriority.Prefix += *spIt++;
						}
						if (StreamPriority.Prefix.Len() == 0)
						{
							return false;
						}
						if (spIt && *spIt == TCHAR('='))
						{
							++spIt;
							if (!ParsePriority(StreamPriority.Priority, spIt, false))
							{
								return false;
							}
						}
						else
						{
							return false;
						}
					}
					ClassPriority.StreamPriorities.Emplace(MoveTemp(StreamPriority));
				}
			}
			// Either there's a comma separating successive entries or we are done.
			SkipWhiteSpaces(it);
			if (it && *it != TCHAR(','))
			{
				return false;
			}
			++it;
			ClassPriorities.Emplace(MoveTemp(ClassPriority));
		}
		return true;
	}

	int32 FCodecSelectionPriorities::GetClassPriority(const FString& CodecSpecifierRFC6381) const
	{
		// If no priorities are given then all have the same priority of 0.
		if (!ClassPriorities.Num())
		{
			return 0;
		}
		// Otherwise apply the priority filter. If no match then return -1.
		for(auto &CodecClass : ClassPriorities)
		{
			if (CodecSpecifierRFC6381.StartsWith(CodecClass.Prefix, ESearchCase::IgnoreCase))
			{
				return CodecClass.Priority;
			}
		}
		return -1;
	}

	int32 FCodecSelectionPriorities::GetStreamPriority(const FString& CodecSpecifierRFC6381) const
	{
		for(auto &CodecClass : ClassPriorities)
		{
			if (CodecSpecifierRFC6381.StartsWith(CodecClass.Prefix, ESearchCase::IgnoreCase))
			{
				for(auto &CodecStream : CodecClass.StreamPriorities)
				{
					if (CodecSpecifierRFC6381.StartsWith(CodecStream.Prefix, ESearchCase::IgnoreCase))
					{
						return CodecStream.Priority;
					}
				}
			}
		}
		return -1;
	}

} // namespace Electra
