// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Output/VCamOutputProviderBase.h"

#include "HAL/Platform.h"
#include "UObject/WeakObjectPtrTemplates.h"

class AActor;
class UVCamComponent;
enum class EVCamTargetViewportID : uint8;

namespace UE::VCamCore
{
	struct FActorLockContext
	{
		/**
		 * The output provider for which is being locked.
		 * Its outer actor will be locking the viewport.
		 * 
		 * Can be used e.g. by the PIE implementation of IViewportLocker to ask the provider's UGameplayViewTargetPolicy to determine
		 * the APlayerController for which to change the viewport.
		 */
		UVCamOutputProviderBase* ProviderToLock;

		AActor* GetLockActor() const { return ProviderToLock ? ProviderToLock->GetTypedOuter<AActor>() : nullptr; }
		bool ShouldLock() const { return ProviderToLock != nullptr; }
	};
	
	/**
	 * Abstracts the viewport system.
	 * This interface only contains the functions used by FViewportLockManager, i.e. those needed for viewport locking.
	 * 
	 * This is implemented differently depending on the platform:
	 * - In editor, it uses the real viewport system (unless in PIE)
	 * - In PIE, it uses the player viewport (ignores the viewport ID)
	 * - In shipped applications, it uses the player viewport (ignores the viewport ID)
	 * 
	 * This also allows mocking in tests.
	 */
	class IViewportLocker 
	{
	public:

		/** Gets the lock actor. See FLevelEditorViewportClient::GetActorLock */
		virtual TWeakObjectPtr<AActor> GetActorLock(EVCamTargetViewportID ViewportID) const = 0;
		/** Gets the cinematic lock actor. See FLevelEditorViewportClient::GetCinematicActorLock */
		virtual TWeakObjectPtr<AActor> GetCinematicActorLock(EVCamTargetViewportID ViewportID) const = 0;

		/** Gets whether GetActorLock is being locked to the viewport. See FLevelEditorViewportClient::bLockedCameraView. */
		virtual bool ShouldLockViewport(EVCamTargetViewportID ViewportID) const = 0;

		/** Sets the lock actor. */
		virtual void SetActorLock(EVCamTargetViewportID ViewportID, const FActorLockContext& LockInfo) = 0;

		virtual ~IViewportLocker() = default;
	};
}
