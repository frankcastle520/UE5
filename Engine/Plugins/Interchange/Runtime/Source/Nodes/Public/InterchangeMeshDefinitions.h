// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/NameTypes.h"
#include "Types/AttributeStorage.h"

#include "InterchangeMeshDefinitions.generated.h"

UENUM(BlueprintType)
enum class EInterchangeMeshCollision : uint8
{
	/** Generates a new box collision mesh encompassing the static mesh*/
	Box,

	/** Generates a new sphere collision mesh encompassing the static mesh*/
	Sphere,

	/** Generates a new capsule collision mesh encompassing the static mesh*/
	Capsule,

	/** Generates a new axis-aligned box collision mesh with the 4 X-axis aligned edges beveled (10 total sides) encompassing the static mesh*/
	Convex10DOP_X UMETA(DisplayName="10DOP-X Simplified"),

	/** Generates a new axis-aligned box collision mesh with the 4 Y-axis aligned edges beveled (10 total sides) encompassing the static mesh*/
	Convex10DOP_Y UMETA(DisplayName = "10DOP-Y Simplified"),

	/** Generates a new axis-aligned box collision mesh with the 4 Z-axis aligned edges beveled (10 total sides) encompassing the static mesh*/
	Convex10DOP_Z UMETA(DisplayName = "10DOP-Z Simplified"),

	/** Generates a new axis-aligned box collision mesh with all edges beveled (18 total sides) encompassing the static mesh*/
	Convex18DOP UMETA(DisplayName = "18DOP Simplified"),
	
	/** Generates a new axis-aligned box collision mesh with all edges and corners beveled (26 total sides) encompassing the static mesh*/
	Convex26DOP UMETA(DisplayName = "26DOP Simplified"),

	/** Generates no collisions, but continue to import custom collisions if the file has ones*/
	None = 0xFF
};

namespace UE
{
	namespace Interchange
	{
		template<> struct TAttributeTypeTraits<EInterchangeMeshCollision>
		{
			static constexpr EAttributeTypes GetType()
			{
				return EAttributeTypes::UInt8;
			}
			static FString ToString(const uint16& Value)
			{
				int32 ValueConv = Value;
				FStringFormatOrderedArguments OrderedArguments;
				OrderedArguments.Add(FStringFormatArg(ValueConv));
				return FString::Format(TEXT("{0}"), OrderedArguments);
			}
		};
	}
}