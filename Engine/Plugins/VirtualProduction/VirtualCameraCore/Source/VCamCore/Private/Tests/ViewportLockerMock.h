// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EVCamTargetViewportID.h"
#include "Util/Viewport/Interfaces/IViewportLocker.h"

#include "HAL/Platform.h"
#include "UObject/WeakObjectPtrTemplates.h"

class AActor;

namespace UE::VCamCore
{
	class FViewportLockerMock : public IViewportLocker
	{
	public:

		static_assert(static_cast<int32>(EVCamTargetViewportID::Viewport1) == 0);

		bool LockedViewports[4] = { false, false, false, false };
		AActor* LockActors[4] = { nullptr, nullptr, nullptr, nullptr };
		AActor* FakeCinematicLocks[4] = { nullptr, nullptr, nullptr, nullptr };
		
		virtual TWeakObjectPtr<AActor> GetActorLock(EVCamTargetViewportID ViewportID) const override { return LockActors[static_cast<int32>(ViewportID)]; }
		virtual TWeakObjectPtr<AActor> GetCinematicActorLock(EVCamTargetViewportID ViewportID) const override { return FakeCinematicLocks[static_cast<int32>(ViewportID)]; }
		virtual bool ShouldLockViewport(EVCamTargetViewportID ViewportID) const override { return LockedViewports[static_cast<int32>(ViewportID)]; }
		virtual void SetActorLock(EVCamTargetViewportID ViewportID, const FActorLockContext& LockInfo) override
		{
			LockActors[static_cast<int32>(ViewportID)] = LockInfo.GetLockActor();
			LockedViewports[static_cast<int32>(ViewportID)] = LockInfo.ShouldLock();
		}
	};
}

