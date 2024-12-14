// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "PlayAnimStatus.generated.h"

/**
 * PlayAnim Request Status
 */
UENUM()
enum class EAnimNextPlayAnimStatus : uint8
{
	//////////////////////////////////////////////////////////////////////////
	// Primary status is held in the bottom 5 bits
	// These are mutually exclusive

	// No current status
	None			= 0x00,

	// Animation request has been queue but hasn't been processed yet
	Pending			= 0x01,

	// Animation is playing (may be blending in or out, may be interrupted)
	Playing			= 0x02,

	// Animation has finished playing (may have the interrupted flag to signal that it didn't stop on its own)
	Completed		= 0x04,

	// Animation request expired without playing
	Expired			= 0x10,


	//////////////////////////////////////////////////////////////////////////
	// Additive status is held in the top 3 bits

	// Animation was interrupted by another animation request (or a stop request)
	Interrupted		= 0x40,

	// Animation is blending out
	BlendingOut		= 0x80,
};

ENUM_CLASS_FLAGS(EAnimNextPlayAnimStatus);

namespace UE::AnimNext
{
	// Create a namespaced alias to simplify usage
	using EPlayAnimStatus = EAnimNextPlayAnimStatus;
}
