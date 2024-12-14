// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EditorViewportSystem.h"
#include "GameViewportSystem.h"
#include "Algo/AnyOf.h"
#include "Engine/GameEngine.h"
#include "Util/Viewport/Interfaces/IViewportGetter.h"
#include "Util/Viewport/Interfaces/IViewportLocker.h"
#include "Util/Viewport/Interfaces/IViewportResolutionChanger.h"

namespace UE::VCamCore
{
	/**
	 * Switches between systems depending on whether we're in PIE or not.
	 *
	 * Depending on the platform, this uses a different system.
	 * - In shipped applications, it uses the player viewport (ignores the viewport ID)
	 * - In PIE, it uses the player viewport (ignores the viewport ID)
	 * - In editor, it uses the real viewport system (unless in PIE)
	 */
	class FViewportSystemSwitcher
		: public IViewportLocker
		, public IViewportResolutionChanger
		, public IViewportGetter
	{
	public:

		//~ Begin IViewportLocker Interface
		virtual TWeakObjectPtr<AActor> GetActorLock(EVCamTargetViewportID ViewportID) const override { return GetViewportLocker().GetActorLock(ViewportID); }
		virtual TWeakObjectPtr<AActor> GetCinematicActorLock(EVCamTargetViewportID ViewportID) const override { return GetViewportLocker().GetCinematicActorLock(ViewportID); }
		virtual bool ShouldLockViewport(EVCamTargetViewportID ViewportID) const override { return GetViewportLocker().ShouldLockViewport(ViewportID); }
		virtual void SetActorLock(EVCamTargetViewportID ViewportID, const FActorLockContext& LockInfo) override { return GetViewportLocker().SetActorLock(ViewportID, LockInfo); }
		//~ End IViewportLocker Interface
		
		//~ Begin IViewportLocker Interface
		virtual void ApplyOverrideResolutionForViewport(EVCamTargetViewportID ViewportID, uint32 NewViewportSizeX, uint32 NewViewportSizeY) override { GetResolutionChanger().ApplyOverrideResolutionForViewport(ViewportID, NewViewportSizeX, NewViewportSizeY); }
		virtual void RestoreOverrideResolutionForViewport(EVCamTargetViewportID ViewportID) override { GetResolutionChanger().RestoreOverrideResolutionForViewport(ViewportID); }
		//~ End IViewportLocker Interface
		
		//~ Begin IViewportLocker Interface
		virtual TSharedPtr<FSceneViewport> GetSceneViewport(EVCamTargetViewportID ViewportID) const override { return GetViewportGetter().GetSceneViewport(ViewportID); }
		virtual TWeakPtr<SWindow> GetInputWindow(EVCamTargetViewportID ViewportID) const override { return GetViewportGetter().GetInputWindow(ViewportID); }
#if WITH_EDITOR
		virtual FLevelEditorViewportClient* GetEditorViewportClient(EVCamTargetViewportID ViewportID) const override { return GetViewportGetter().GetEditorViewportClient(ViewportID); }
#endif
		//~ End IViewportLocker Interface

	private:

#if WITH_EDITOR
		FEditorViewportSystem EditorSystem;
#endif
		FGameViewportSystem GameSystem;

		const IViewportLocker& GetViewportLocker() const { return GetInterface<IViewportLocker>(); }
		IViewportLocker& GetViewportLocker() { return GetInterface<IViewportLocker>(); }
		const IViewportResolutionChanger& GetResolutionChanger() const { return GetInterface<IViewportResolutionChanger>(); }
		IViewportResolutionChanger& GetResolutionChanger() { return GetInterface<IViewportResolutionChanger>(); }
		const IViewportGetter& GetViewportGetter() const { return GetInterface<IViewportGetter>(); }
		IViewportGetter& GetViewportGetter() { return GetInterface<IViewportGetter>(); }

		template<typename T>
		const T& GetInterface() const
		{
			return const_cast<FViewportSystemSwitcher*>(this)->GetInterface<T>();
		}

		template<typename T>
		T& GetInterface()
		{
#if WITH_EDITOR
			// Handle Standalone mode.
			if (Cast<UGameEngine>(GEngine))
			{
				return GameSystem;
			}
			
			bool bIsGameWorld = Algo::AnyOf(GEngine->GetWorldContexts(), [](const FWorldContext& Context)
			{
				return Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE || Context.WorldType == EWorldType::GamePreview;
			});
		
			return bIsGameWorld ? static_cast<T&>(GameSystem) : EditorSystem;
#else
			return GameSystem;
#endif
		}
	};
}
