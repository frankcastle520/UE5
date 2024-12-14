// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EVCamTargetViewportID.h"
#include "VCamViewportLocker.generated.h"

USTRUCT()
struct FVCamViewportLockState
{
	GENERATED_BODY()
	
	/** Whether the user wants the viewport to be locked */
	UPROPERTY(EditAnywhere, Category = "Viewport")
	bool bLockViewportToCamera = false;
};

/** Keeps track of which viewports are locked */
USTRUCT()
struct FVCamViewportLocker
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, EditFixedSize, Category = "Viewport")
	TMap<EVCamTargetViewportID, FVCamViewportLockState> Locks {
		{ EVCamTargetViewportID::Viewport1, {} },
		{ EVCamTargetViewportID::Viewport2, {} },
		{ EVCamTargetViewportID::Viewport3, {} },
		{ EVCamTargetViewportID::Viewport4, {} }
	};
	
	bool ShouldLock(EVCamTargetViewportID ViewportID) const
	{
		return Locks[ViewportID].bLockViewportToCamera;
	}
	
	FVCamViewportLocker& SetLockState(EVCamTargetViewportID ViewportID, bool bShouldLock)
	{
		Locks[ViewportID].bLockViewportToCamera = bShouldLock;
		return *this;
	}

	friend bool operator==(const FVCamViewportLocker& Left, const FVCamViewportLocker& Right)
	{
		return Left.Locks[EVCamTargetViewportID::Viewport1].bLockViewportToCamera == Right.Locks[EVCamTargetViewportID::Viewport1].bLockViewportToCamera
			&& Left.Locks[EVCamTargetViewportID::Viewport2].bLockViewportToCamera == Right.Locks[EVCamTargetViewportID::Viewport2].bLockViewportToCamera
			&& Left.Locks[EVCamTargetViewportID::Viewport3].bLockViewportToCamera == Right.Locks[EVCamTargetViewportID::Viewport3].bLockViewportToCamera
			&& Left.Locks[EVCamTargetViewportID::Viewport4].bLockViewportToCamera == Right.Locks[EVCamTargetViewportID::Viewport4].bLockViewportToCamera;
	}

	friend bool operator!=(const FVCamViewportLocker& Left, const FVCamViewportLocker& Right)
	{
		return !(Left == Right);
	}
};
