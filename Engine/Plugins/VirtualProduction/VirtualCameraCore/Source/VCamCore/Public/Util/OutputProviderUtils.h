// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "Misc/EnumClassFlags.h"

class FString;
class UVCamOutputProviderBase;

namespace UE::VCamCore
{
	/** @return Gets the output provider at Index int the UVCamComponent that owns OutputProvider. */
	VCAMCORE_API UVCamOutputProviderBase* GetOtherOutputProviderByIndex(const UVCamOutputProviderBase& OutputProvider, int32 Index);

	/** @return Gets the index of OutputProvider in the UVCamComponent that owns OutputProvider. */
	VCAMCORE_API int32 FindOutputProviderIndex(const UVCamOutputProviderBase& OutputProvider);

	/** Flags for GenerateUniqueOutputProviderName. */
	enum class ENameGenerationFlags : uint8
	{
		None,
		/**
		 * The name should not contain the output provider index.
		 * The name is no longer guaranteed to be unique.
		 *
		 * Use this if you want the generated name to be the actor label or internal name depending on whether the actor label is unique.
		 */
		SkipAppendingIndex = 1 << 0
	};
	ENUM_CLASS_FLAGS(ENameGenerationFlags);
	
	/**
	 * Generates a unique name for OutputProvider following the pattern %s_%d ([ActorLabel]_[OutputProviderIndex]) where
	 * - %s is the label of the owning actor if it is unique across all actors having a UVCamComponent in the world, and the owning actor's name otherwise
	 * - %d is the result of FindOutputProviderIndex
	 * @param OutputProvider The provider for which to generate the unique name
	 * @param Flags Flags to use for this operation
	 */
	VCAMCORE_API FString GenerateUniqueOutputProviderName(const UVCamOutputProviderBase& OutputProvider, ENameGenerationFlags Flags = ENameGenerationFlags::None);
}
