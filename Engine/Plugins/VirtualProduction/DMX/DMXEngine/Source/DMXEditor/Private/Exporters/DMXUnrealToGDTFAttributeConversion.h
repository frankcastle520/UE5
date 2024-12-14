// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Map.h"
#include "Internationalization/Regex.h"
#include "UObject/NameTypes.h"

namespace UE::DMX::GDTF
{
	class FDMXUnrealToGDTFAttributeConversion
	{
	public:
		/** Creates a GDTF attribute from an unreal attribute */
		static FName ConvertUnrealToGDTFAttribute(const FName& InUnrealAttribute)
		{			
			// Remove, but remember trailing numbers
			FString CleanAttribute;
			FString TrailingNumberString; 

			const FRegexPattern RegexPattern(TEXT("(f\\d+oo)(\\d+)$"));
			FRegexMatcher RegexMatcher(RegexPattern, InUnrealAttribute.ToString());
			if (RegexMatcher.FindNext())
			{
				CleanAttribute = *RegexMatcher.GetCaptureGroup(0);
				TrailingNumberString = RegexMatcher.GetCaptureGroup(1);
			}
			else
			{
				CleanAttribute = InUnrealAttribute.ToString();
			}

			const FName* AttributeNamePtr = UnrealToGDTFAttributeMap.Find(*CleanAttribute);
			if (AttributeNamePtr)
			{
				return *((*AttributeNamePtr).ToString() + TrailingNumberString);
			}
			else
			{
				return InUnrealAttribute;
			}
		}

		static FName GetPrettyFromGDTFAttribute(const FName& InGDTFAttribute)
		{
			const FName* PrettyPtr = GDTFAttributeToPrettyMap.Find(InGDTFAttribute);
			if (PrettyPtr)
			{
				return (*PrettyPtr);
			}
			else
			{
				return InGDTFAttribute;
			}
		}

		/** Creates a GDTF feature group for a GDTF Attribute */
		static FName GetFeatureGroupForGDTFAttribute(const FName& InGDTFAttribute)
		{
			const TTuple<FName, FName>* FeaturePairPtr = GDTFAttributeToFeatureMap.Find(InGDTFAttribute);
			if (FeaturePairPtr)
			{
				return (*FeaturePairPtr).Key;
			}
			else
			{
				return "Control";
			}
		}

		/** Creates a GDTF feature for a GDTF Attribute */
		static FName GetFeatureForGDTFAttribute(const FName& InGDTFAttribute)
		{
			const TPair<FName, FName>* FeaturePairPtr = GDTFAttributeToFeatureMap.Find(InGDTFAttribute);
			if (FeaturePairPtr)
			{
				return (*FeaturePairPtr).Value;
			}
			else
			{
				return "Control";
			}
		}

	private:
		/** Conversion from Unreal attributes to GDTF attributes. See also how UDMXProtocolSettings::Attributes is initialized and applied. */
		static inline const TMap<FName, FName> UnrealToGDTFAttributeMap =
		{
			{ "Intensity", "Dimmer" },
			{ "Strength", "Dimmer" },
			{ "Brightness", "Dimmer" },

			{ "Red", "ColorAdd_R" },
			{ "Green", "ColorAdd_G" },
			{ "Blue", "ColorAdd_B" },
			{ "Cyan", "ColorAdd_C" },
			{ "Magenta", "ColorAdd_M" },
			{ "Yellow", "ColorAdd_Y" },
			{ "White", "ColorAdd_W" },
			{ "Amber", "ColorAdd_A" },

			{ "Gobo Spin", "GoboSpin" },
			{ "Gobo Wheel Rotate", "GoboWheel" }
		};

		/** Defines pretty attribute names for GDTF attribute names */
		static inline const TMap<FName, FName> GDTFAttributeToPrettyMap =
		{
			{ "Dimmer", "Dim" },

			{ "ColorAdd_R", "R" },
			{ "ColorAdd_G", "G" },
			{ "ColorAdd_B", "B" },
			{ "ColorAdd_C", "C" },
			{ "ColorAdd_M", "M" },
			{ "ColorAdd_Y", "Y" },
			{ "ColorAdd_W", "W" },
			{ "ColorAdd_A", "A" },

			{ "Pan", "P" },
			{ "Tilt", "T" }
		};

		/** 
		 * Defines attributes that should be assigned to a specific feature group. 
		 * 
		 * Assumes that GDTF and not Unreal attributes are used.
		 */
		static inline const TMap<FName, TPair<FName, FName>> GDTFAttributeToFeatureMap =
		{
			// Dimmer feature group
			{ "Dimmer",			{ "Dimmer", "Dimmer" } },

			// Color feature group
			{ "Color",			{ "Color", "Color" } },
			{ "CTC",			{ "Color", "Color" } },
			{ "ColorAdd_R",		{ "Color", "RGB" } },
			{ "ColorAdd_G",		{ "Color", "RGB" } },
			{ "ColorAdd_B",		{ "Color", "RGB" } },
			{ "ColorAdd_C",		{ "Color", "RGB" } },
			{ "ColorAdd_M",		{ "Color", "RGB" } },
			{ "ColorAdd_Y",		{ "Color", "RGB" } },
			{ "ColorAdd_W",		{ "Color", "RGB" } },
			{ "ColorAdd_A",		{ "Color", "RGB" } },
			{ "CIE_X",			{ "Color", "CIE" } },
			{ "CIE_Y",			{ "Color", "CIE" } },
			{ "CIE_Brightness", { "Color", "CIE" } },

			// Position feature group
			{ "Pan",			{ "Position", "PanTilt" } },
			{ "Tilt",			{ "Position", "PanTilt" } },

			// Gobo feature group
			{ "GoboSpin",		{ "Gobo", "Gobo" } },
			{ "GoboWheel",		{ "Gobo", "Gobo" } },

			// Focus feature group
			{ "Focus",			{ "Focus", "Focus" } },
			{ "Zoom",			{ "Focus", "Focus" } },

			// Beam feature group
			{ "Shutter",		{ "Beam", "Beam" } },
			{ "Frost",			{ "Beam", "Beam" } }
		};
	};
}
