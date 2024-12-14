// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Evaluation/IMovieScenePlaybackCapability.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"

class UCameraComponent;

namespace UE::MovieScene
{
	struct FCameraCutViewTargetCacheCapability
	{
		/** Playback capability ID */
		static const TPlaybackCapabilityID<FCameraCutViewTargetCacheCapability> ID;
		
		/** The last evaluated view target. */
		TWeakObjectPtr<UCameraComponent> LastViewTargetCamera;
	};
}


