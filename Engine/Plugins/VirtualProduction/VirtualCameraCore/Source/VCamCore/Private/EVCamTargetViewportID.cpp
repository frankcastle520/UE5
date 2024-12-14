// Copyright Epic Games, Inc. All Rights Reserved.

#include "EVCamTargetViewportID.h"

#if WITH_EDITOR
#include "Algo/Transform.h"
#include "Algo/Sort.h"
#include "Containers/UnrealString.h"
#include "Engine/Engine.h"
#include "LevelEditorViewport.h"
#include "SLevelViewport.h"

namespace UE::VCamCore
{
	namespace Private
	{
		static FString GetBaseConfigKeyFor(EVCamTargetViewportID TargetViewport)
		{
			/*
			 * Here are example strings for EVCamTargetViewportID == 1: 
			 * One pane: OnePane.Viewport 1.Viewport0
			 * Two pane:
			 *	- Viewport 1.Viewport0
			 *	- Viewport 1.Viewport1
			 * Three pane:
			 *	- ThreePanesLeft.Viewport 1.Viewport0
			 *	- ThreePanesLeft.Viewport 1.Viewport1
			 *	- ThreePanesLeft.Viewport 1.Viewport2
			 * Four pane:
			 *	- FourPanes2x2.Viewport 1.Viewport0
			 *	- FourPanes2x2.Viewport 1.Viewport1
			 *	- FourPanes2x2.Viewport 1.Viewport2
			 *	- FourPanes2x2.Viewport 1.Viewport3
			 */
			return FString::Printf(TEXT("Viewport %d.Viewport"), static_cast<int32>(TargetViewport) + 1);
		}
	}
	
	TSharedPtr<SLevelViewport> GetLevelViewport(EVCamTargetViewportID TargetViewport)
	{
		if (!GEditor)
		{
			return nullptr;
		}
		
		// We consider all layouts that are in perspective mode.
		// However, there can be multiple candidates, e.g. in 2x2 layout:
		//	- in the top-right, there is a button for maximizing.
		//	- in the top-left, you can set the mode to "Perspective"
		//	- in the top-left, you can make the viewport immersive (i.e. take up entire screen)
		// We'll just pick one randomly, but we'll favour whatever viewport takes up the most space (immersive > maximized > rest).
		
		TArray<TSharedPtr<SLevelViewport>> Viewports;
		Algo::TransformIf(GEditor->GetLevelViewportClients(), Viewports,
			[TargetViewport](const FLevelEditorViewportClient* Client)
			{
			   const TSharedPtr<SLevelViewport> LevelViewport = StaticCastSharedPtr<SLevelViewport>(Client->GetEditorViewportWidget());
			   return LevelViewport
				  // E.g. in 2x2 layout you can have several modes, like "Top", "Left". We only care for the "Perspective" mode.
				  && !Client->IsOrtho()
				  // The config key string holds information about TargetViewport
				  && LevelViewport->GetConfigKey().ToString().Contains(Private::GetBaseConfigKeyFor(TargetViewport), ESearchCase::CaseSensitive);
			},
			[](const FLevelEditorViewportClient* Client)
			{
			   return StaticCastSharedPtr<SLevelViewport>(Client->GetEditorViewportWidget());
			});

		Algo::Sort(Viewports, [](const TSharedPtr<SLevelViewport>& Left, const TSharedPtr<SLevelViewport>& Right)
		{
			return Right->IsImmersive() || (Right->IsMaximized() && !Left->IsImmersive());
		});

		return Viewports.IsEmpty()
			? nullptr
			// Sort is ascending
			: Viewports[Viewports.Num() - 1];
	}
}
#endif