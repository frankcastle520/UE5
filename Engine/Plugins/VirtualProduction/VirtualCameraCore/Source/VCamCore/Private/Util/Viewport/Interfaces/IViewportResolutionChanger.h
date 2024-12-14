// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"

enum class EVCamTargetViewportID : uint8;

namespace UE::VCamCore
{
	
	/**
	 * Abstracts the viewport system.
	 * This interface only contains the functions used by FViewportResolutionManager, i.e. those needed for viewport resolution changing.
	 * 
	 * This is implemented differently depending on the platform:
	 * - In editor, it uses the real viewport system (unless in PIE)
	 * - In PIE, it uses the player viewport (ignores the viewport ID)
	 * - In shipped applications, it uses the player viewport (ignores the viewport ID)
	 * 
	 * This also allows mocking in tests.
	 */
	class IViewportResolutionChanger 
	{
	public:

		/** Sets the override resolution of the given viewport. See FSceneViewport::SetFixedViewportSize. */
		virtual void ApplyOverrideResolutionForViewport(EVCamTargetViewportID ViewportID, uint32 NewViewportSizeX, uint32 NewViewportSizeY) = 0;

		/** Clears the resolution override of the given viewport. See FSceneViewport::SetFixedViewportSize */
		virtual void RestoreOverrideResolutionForViewport(EVCamTargetViewportID ViewportID) = 0;

		virtual ~IViewportResolutionChanger() = default;
	};
}
