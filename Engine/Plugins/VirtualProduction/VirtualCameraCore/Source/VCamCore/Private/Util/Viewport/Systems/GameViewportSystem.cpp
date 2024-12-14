// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameViewportSystem.h"

#include "LogVCamCore.h"
#include "Output/ViewTargetPolicy/GameplayViewTargetPolicy.h"
#include "VCamComponent.h"

#include "Engine/Engine.h"
#include "Engine/GameEngine.h"
#include "Framework/Application/SlateApplication.h"
#include "Slate/SceneViewport.h"
#if WITH_EDITOR
#include "IAssetViewport.h"
#endif

namespace UE::VCamCore
{
	namespace Private
	{
#if WITH_EDITOR
		static TSharedPtr<FSceneViewport> GetPIEViewport()
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.WorldType == EWorldType::PIE)
				{
					FSlatePlayInEditorInfo* SlatePlayInEditorSession = GEditor->SlatePlayInEditorMap.Find(Context.ContextHandle);
					if (SlatePlayInEditorSession)
					{
						if (SlatePlayInEditorSession->DestinationSlateViewport.IsValid())
						{
							const TSharedPtr<IAssetViewport> DestinationLevelViewport = SlatePlayInEditorSession->DestinationSlateViewport.Pin();
							return DestinationLevelViewport->GetSharedActiveViewport();
						}
						if (SlatePlayInEditorSession->SlatePlayInEditorWindowViewport.IsValid())
						{
							return SlatePlayInEditorSession->SlatePlayInEditorWindowViewport;
						}
					}
				}
			}
			return nullptr;
		}
#endif
	}
	
	TWeakObjectPtr<AActor> FGameViewportSystem::GetActorLock(EVCamTargetViewportID ViewportID) const
	{
		const FLockInfo* LockInfo = LockInfos.Find(ViewportID);
		UVCamOutputProviderBase* LockProvider = LockInfo ? LockInfo->LockProvider.Get() : nullptr;
		return LockProvider ? LockProvider->GetTypedOuter<AActor>() : nullptr;
	}

	bool FGameViewportSystem::ShouldLockViewport(EVCamTargetViewportID ViewportID) const
	{
		const FLockInfo* LockInfo = LockInfos.Find(ViewportID);
		UVCamOutputProviderBase* LockProvider = LockInfo ? LockInfo->LockProvider.Get() : nullptr;
		return LockProvider != nullptr;
	}

	void FGameViewportSystem::SetActorLock(EVCamTargetViewportID ViewportID, const FActorLockContext& Context)
	{
		if (Context.ShouldLock())
		{
			TakeActorLock(ViewportID, Context);
		}
		else
		{
			ReleaseViewTarget(ViewportID);
		}
	}

	void FGameViewportSystem::ApplyOverrideResolutionForViewport(EVCamTargetViewportID ViewportID, uint32 NewViewportSizeX, uint32 NewViewportSizeY)
	{
#if WITH_EDITOR
		if (const TSharedPtr<FSceneViewport> PIEViewport = Private::GetPIEViewport())
		{
			PIEViewport->SetFixedViewportSize(NewViewportSizeX, NewViewportSizeY);
		}
#else 
		UE_LOG(LogVCamCore, Warning, TEXT("ApplyOverrideResolutionForViewport: Override resolution not implemented for games"));
#endif
	}

	void FGameViewportSystem::RestoreOverrideResolutionForViewport(EVCamTargetViewportID ViewportID)
	{
#if WITH_EDITOR
		if (const TSharedPtr<FSceneViewport> PIEViewport = Private::GetPIEViewport())
		{
			PIEViewport->SetFixedViewportSize(0, 0);
		}
#else
		UE_LOG(LogVCamCore, Warning, TEXT("RestoreOverrideResolutionForViewport: Override resolution not implemented for games"));
#endif
	}

	TSharedPtr<FSceneViewport> FGameViewportSystem::GetSceneViewport(EVCamTargetViewportID ViewportID) const
	{
		if (UGameEngine* GameEngine = Cast<UGameEngine>(GEngine))
		{
			return GameEngine->SceneViewport;
		}

#if WITH_EDITOR
		if (GIsEditor && GEngine)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.WorldType == EWorldType::PIE)
				{
					FSlatePlayInEditorInfo* SlatePlayInEditorSession = GEditor->SlatePlayInEditorMap.Find(Context.ContextHandle);
				
					if (SlatePlayInEditorSession && SlatePlayInEditorSession->DestinationSlateViewport.IsValid())
					{
						TSharedPtr<IAssetViewport> DestinationLevelViewport = SlatePlayInEditorSession->DestinationSlateViewport.Pin();
						return DestinationLevelViewport->GetSharedActiveViewport();
					}
					if (SlatePlayInEditorSession && SlatePlayInEditorSession->SlatePlayInEditorWindowViewport.IsValid())
					{
						return SlatePlayInEditorSession->SlatePlayInEditorWindowViewport;
					}
				}
			}
		}
#endif
		
		UE_LOG(LogVCamCore, Warning, TEXT("GetSceneViewport: No viewport window found for gameplay logic"));
		return nullptr;
	}

	TWeakPtr<SWindow> FGameViewportSystem::GetInputWindow(EVCamTargetViewportID ViewportID) const
	{
		if (UGameEngine* GameEngine = Cast<UGameEngine>(GEngine))
		{
			return GameEngine->GameViewportWindow;
		}
		
#if WITH_EDITOR
		if (GIsEditor && GEngine)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.WorldType == EWorldType::PIE)
				{
					FSlatePlayInEditorInfo* SlatePlayInEditorSession = GEditor->SlatePlayInEditorMap.Find(Context.ContextHandle);
				
					if (SlatePlayInEditorSession && SlatePlayInEditorSession->DestinationSlateViewport.IsValid())
					{
						TSharedPtr<IAssetViewport> DestinationLevelViewport = SlatePlayInEditorSession->DestinationSlateViewport.Pin();
						return FSlateApplication::Get().FindWidgetWindow(DestinationLevelViewport->AsWidget());
					}
					if (SlatePlayInEditorSession && SlatePlayInEditorSession->SlatePlayInEditorWindowViewport.IsValid())
					{
						return SlatePlayInEditorSession->SlatePlayInEditorWindow;
					}
				}
			}
		}
#endif

		UE_LOG(LogVCamCore, Warning, TEXT("GetInputWindow: No viewport window found for gameplay logic"));
		return nullptr;
	}

	void FGameViewportSystem::TakeActorLock(EVCamTargetViewportID ViewportID, const FActorLockContext& Context)
	{
		UVCamOutputProviderBase* LockProvider = Context.ProviderToLock;
		UGameplayViewTargetPolicy* ViewTargetPolicy = LockProvider ? LockProvider->GetGameplayViewTargetPolicy() : nullptr;
		UVCamComponent* OwnerComponent = LockProvider ? LockProvider->GetVCamComponent() : nullptr;
		UCineCameraComponent* TargetCamera = OwnerComponent ? OwnerComponent->GetTargetCamera() : nullptr;
		FLockInfo* LockInfo = LockInfos.Find(ViewportID);
		const bool bAlreadyLocked = LockInfo && LockInfo->LockProvider == LockProvider;
		if (bAlreadyLocked || !TargetCamera || !ViewTargetPolicy)
		{
			return;
		}

		if (LockInfo)
		{
			ReleaseViewTarget(ViewportID);
		}
			
		constexpr bool bWillBeActive = true;
		const FDeterminePlayerControllersTargetPolicyParams DeterminePlayersParams{ LockProvider, TargetCamera, bWillBeActive };
		const TArray<APlayerController*> PlayerControllers = ViewTargetPolicy->DeterminePlayerControllers(DeterminePlayersParams);

		FUpdateViewTargetPolicyParams UpdateViewTargetParams {{ DeterminePlayersParams }};
		Algo::Transform(PlayerControllers, UpdateViewTargetParams.PlayerControllers, [](APlayerController* PC){ return PC; });
		LockInfo = LockInfo ? LockInfo : &LockInfos.Add(ViewportID);
		Algo::Transform(PlayerControllers, LockInfo->PlayersWhoseViewTargetWasSet, [](APlayerController* PC){ return PC; });
	
		ViewTargetPolicy->UpdateViewTarget(UpdateViewTargetParams);
	}

	void FGameViewportSystem::ReleaseViewTarget(EVCamTargetViewportID ViewportID)
	{
		FLockInfo LockInfo;
		const bool bWasRemoved = LockInfos.RemoveAndCopyValue(ViewportID, LockInfo);
		if (!bWasRemoved)
		{
			return;
		}

		UVCamOutputProviderBase* LockProvider = LockInfo.LockProvider.Get();
		UGameplayViewTargetPolicy* ViewTargetPolicy = LockProvider ? LockProvider->GetGameplayViewTargetPolicy() : nullptr;
		UVCamComponent* OwnerComponent = LockProvider ? LockProvider->GetVCamComponent() : nullptr;
		UCineCameraComponent* TargetCamera = OwnerComponent ? OwnerComponent->GetTargetCamera() : nullptr;
		if (!TargetCamera || !ViewTargetPolicy)
		{
			return;
		}

		constexpr bool bWillBeActive = false;
		FUpdateViewTargetPolicyParams Params { { LockProvider, TargetCamera, bWillBeActive } };
		Algo::TransformIf(LockInfo.PlayersWhoseViewTargetWasSet, Params.PlayerControllers,
			[](TWeakObjectPtr<APlayerController> PC){ return PC.IsValid(); },
			[](TWeakObjectPtr<APlayerController> PC){ return PC.Get(); }
		);
		ViewTargetPolicy->UpdateViewTarget(Params);
	}
}
