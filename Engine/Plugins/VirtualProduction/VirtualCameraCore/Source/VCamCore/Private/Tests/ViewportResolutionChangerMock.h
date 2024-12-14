// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EVCamTargetViewportID.h"

#include "HAL/Platform.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "Util/Viewport/Interfaces/IViewportResolutionChanger.h"

class AActor;

namespace UE::VCamCore
{
	class FViewportResolutionChangerMock : public IViewportResolutionChanger
	{
	public:

		static_assert(static_cast<int32>(EVCamTargetViewportID::Viewport1) == 0);
		
		FIntPoint OverrideResolutions[4] = { {}, {}, {}, {} };

		virtual void ApplyOverrideResolutionForViewport(EVCamTargetViewportID ViewportID, uint32 NewViewportSizeX, uint32 NewViewportSizeY) override { OverrideResolutions[static_cast<int32>(ViewportID)] = FIntPoint(NewViewportSizeX, NewViewportSizeY); }
		virtual void RestoreOverrideResolutionForViewport(EVCamTargetViewportID ViewportID) override { OverrideResolutions[static_cast<int32>(ViewportID)] = {}; }
	};
}

