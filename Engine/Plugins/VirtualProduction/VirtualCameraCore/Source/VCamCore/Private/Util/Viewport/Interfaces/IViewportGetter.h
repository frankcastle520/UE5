// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "Templates/SharedPointer.h"

class FSceneViewport;
class SWindow;
enum class EVCamTargetViewportID : uint8;

#if WITH_EDITOR
class FLevelEditorViewportClient;
#endif

namespace UE::VCamCore
{
	/**
	 * Abstracts the viewport system.
	 * This interface only contains those functions required by FViewportManager, i.e. those that UVCamOutputProviderBase uses to query viewport information.
	 * 
	 * This is implemented differently depending on the platform:
	 * - In editor, it uses the real viewport system (unless in PIE)
	 * - In PIE, it uses the player viewport (ignores the viewport ID)
	 * - In shipped applications, it uses the player viewport (ignores the viewport ID)
	 */
	class IViewportGetter 
	{
	public:

		/** @return Gets the scene viewport that is identified by ViewportID. */
		virtual TSharedPtr<FSceneViewport> GetSceneViewport(EVCamTargetViewportID ViewportID) const = 0;

		/** @return The window which contains the viewport identified by ViewportID. Some output providers uses this to route input, e.g. pixel streaming. */
		virtual TWeakPtr<SWindow> GetInputWindow(EVCamTargetViewportID ViewportID) const = 0;

#if WITH_EDITOR
		/**
		 * Gets the FLevelEditorViewportClient that is managing the ViewportID.
		 * This will only return valid in the editor environment (thus not work in PIE or games).
		 * 
		 * If the UVCamOutputProviderBase::DisplayType is EVPWidgetDisplayType::PostProcessWithBlendMaterial, the FEditorViewportClient::ViewModifiers
		 * are used to overlay the widget into the viewport because it plays nicely with other viewports. In games, the output provider simply places
		 * the post process material in the target camera because there is only one viewport.
		 * Hence, there is no implementation for games.
		 */
		virtual FLevelEditorViewportClient* GetEditorViewportClient(EVCamTargetViewportID ViewportID) const { return nullptr; }
#endif

		virtual ~IViewportGetter() = default;
	};
}
