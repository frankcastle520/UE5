// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TraitCore/ITraitInterface.h"

namespace UE::AnimNext
{
	/**
	 * IScopedTraitInterface
	 * 
	 * Base type for all scoped trait interfaces. Used for type safety.
	 */
	struct ANIMNEXT_API IScopedTraitInterface : ITraitInterface
	{
		DECLARE_ANIM_TRAIT_INTERFACE(IScopedTraitInterface, 0x621b01eb)
	};
}
