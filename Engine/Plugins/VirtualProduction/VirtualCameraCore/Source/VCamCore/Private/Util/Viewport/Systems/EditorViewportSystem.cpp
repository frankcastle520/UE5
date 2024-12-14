// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorViewportSystem.h"

#if WITH_EDITOR
#include "Framework/Application/SlateApplication.h"
#include "SLevelViewport.h"
#include "Slate/SceneViewport.h"

namespace UE::VCamCore
{
	namespace Private
	{
		TSharedPtr<FSceneViewport> GetSceneViewport(EVCamTargetViewportID ViewportID)
		{
			const TSharedPtr<SLevelViewport> Viewport = GetLevelViewport(ViewportID);
			const FLevelEditorViewportClient* ViewportClient = Viewport ? &Viewport->GetLevelViewportClient() : nullptr;
			const TSharedPtr<SEditorViewport> ViewportWidget = ViewportClient ? ViewportClient->GetEditorViewportWidget() : nullptr;
			return ViewportClient ? ViewportWidget->GetSceneViewport() : nullptr;
		}
	}
	
	TWeakObjectPtr<AActor> FEditorViewportSystem::GetActorLock(EVCamTargetViewportID ViewportID) const
	{
		const TSharedPtr<SLevelViewport> Viewport = GetLevelViewport(ViewportID);
		FLevelEditorViewportClient* ViewportClient = Viewport ? &Viewport->GetLevelViewportClient() : nullptr;
		return ViewportClient ? ViewportClient->GetActorLock().LockedActor : nullptr;
	}

	TWeakObjectPtr<AActor> FEditorViewportSystem::GetCinematicActorLock(EVCamTargetViewportID ViewportID) const
	{
		const TSharedPtr<SLevelViewport> Viewport = GetLevelViewport(ViewportID);
		FLevelEditorViewportClient* ViewportClient = Viewport ? &Viewport->GetLevelViewportClient() : nullptr;
		return ViewportClient ? ViewportClient->GetCinematicActorLock().LockedActor : nullptr;
	}

	bool FEditorViewportSystem::ShouldLockViewport(EVCamTargetViewportID ViewportID) const
	{
		const TSharedPtr<SLevelViewport> Viewport = GetLevelViewport(ViewportID);
		FLevelEditorViewportClient* ViewportClient = Viewport ? &Viewport->GetLevelViewportClient() : nullptr;
		return ViewportClient && ViewportClient->bLockedCameraView;
	}

	void FEditorViewportSystem::SetActorLock(EVCamTargetViewportID ViewportID, const FActorLockContext& LockInfo)
	{
		const TSharedPtr<SLevelViewport> Viewport = GetLevelViewport(ViewportID);
		FLevelEditorViewportClient* ViewportClient = Viewport ? &Viewport->GetLevelViewportClient() : nullptr;
		if (!ViewportClient)
		{
			return;
		}

		if (AActor* LockActor = LockInfo.GetLockActor())
		{
			ViewportClient->SetActorLock(LockActor);
			ViewportClient->bLockedCameraView = true;
		}
		else
		{
			ViewportClient->SetActorLock(nullptr);
			ViewportClient->bLockedCameraView = false;
		}
	}

	void FEditorViewportSystem::ApplyOverrideResolutionForViewport(EVCamTargetViewportID ViewportID, uint32 NewViewportSizeX, uint32 NewViewportSizeY)
	{
		if (const TSharedPtr<FSceneViewport> SceneViewport = Private::GetSceneViewport(ViewportID))
		{
			SceneViewport->SetFixedViewportSize(NewViewportSizeX, NewViewportSizeY);
		}
	}

	void FEditorViewportSystem::RestoreOverrideResolutionForViewport(EVCamTargetViewportID ViewportID)
	{
		if (const TSharedPtr<FSceneViewport> SceneViewport = Private::GetSceneViewport(ViewportID))
		{
			SceneViewport->SetFixedViewportSize(0, 0);
		}
	}

	TSharedPtr<FSceneViewport> FEditorViewportSystem::GetSceneViewport(EVCamTargetViewportID ViewportID) const
	{
		const TSharedPtr<SLevelViewport> Viewport = GetLevelViewport(ViewportID);
		return Viewport ? Viewport->GetSceneViewport() : nullptr;
	}

	TWeakPtr<SWindow> FEditorViewportSystem::GetInputWindow(EVCamTargetViewportID ViewportID) const
	{
		const TSharedPtr<SLevelViewport> Viewport = GetLevelViewport(ViewportID);
		return Viewport ? FSlateApplication::Get().FindWidgetWindow(Viewport.ToSharedRef()) : nullptr;
	}

	FLevelEditorViewportClient* FEditorViewportSystem::GetEditorViewportClient(EVCamTargetViewportID ViewportID) const
	{
		const TSharedPtr<SLevelViewport> Viewport = GetLevelViewport(ViewportID);
		return Viewport ? &Viewport->GetLevelViewportClient() : nullptr;
	}
}
#endif
