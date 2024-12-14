// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Util/Viewport/Interfaces/IViewportGetter.h"
#include "Util/Viewport/Interfaces/IViewportLocker.h"
#include "Util/Viewport/Interfaces/IViewportResolutionChanger.h"

namespace UE::VCamCore
{
#if WITH_EDITOR
	/** Uses the editor's multiple viewports. */
	class FEditorViewportSystem
		: public IViewportLocker
		, public IViewportResolutionChanger
		, public IViewportGetter
	{
	public:

		//~ Begin IViewportLocker Interface
		virtual TWeakObjectPtr<AActor> GetActorLock(EVCamTargetViewportID ViewportID) const override;
		virtual TWeakObjectPtr<AActor> GetCinematicActorLock(EVCamTargetViewportID ViewportID) const override;
		virtual bool ShouldLockViewport(EVCamTargetViewportID ViewportID) const override;
		virtual void SetActorLock(EVCamTargetViewportID ViewportID, const FActorLockContext& LockInfo) override;
		//~ End IViewportLocker Interface
		
		//~ Begin IViewportResolutionChanger Interface
		virtual void ApplyOverrideResolutionForViewport(EVCamTargetViewportID ViewportID, uint32 NewViewportSizeX, uint32 NewViewportSizeY) override;
		virtual void RestoreOverrideResolutionForViewport(EVCamTargetViewportID ViewportID) override;
		//~ End IViewportResolutionChanger Interface

		//~ Begin IViewportGetter Interface
		virtual TSharedPtr<FSceneViewport> GetSceneViewport(EVCamTargetViewportID ViewportID) const override;
		virtual TWeakPtr<SWindow> GetInputWindow(EVCamTargetViewportID ViewportID) const override;
		virtual FLevelEditorViewportClient* GetEditorViewportClient(EVCamTargetViewportID ViewportID) const override;
		//~ Begin IViewportGetter Interface
	};
#endif
}
