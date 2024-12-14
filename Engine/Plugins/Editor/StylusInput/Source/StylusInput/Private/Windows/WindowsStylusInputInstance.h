// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <HAL/Platform.h>

#if PLATFORM_WINDOWS

#include <StylusInput.h>
#include <Containers/Array.h>
#include <Templates/UniquePtr.h>

#include <Windows/AllowWindowsPlatformTypes.h>
	#include <Microsoft/COMPointer.h>
	#include <RTSCom.h>
#include <Windows/HideWindowsPlatformTypes.h>

#include "WindowsStylusInputTabletContext.h"

namespace UE::StylusInput
{
	class IStylusInputEventHandler;
}

namespace UE::StylusInput::Private::Windows
{
	class FWindowsStylusInputPluginSync;
	class FWindowsStylusInputPluginAsync;
	class FWindowsStylusInputPlatformAPI;

	class FWindowsStylusInputInstance : public IStylusInputInstance
	{
	public:
		explicit FWindowsStylusInputInstance(HWND OSWindowHandle);
		virtual ~FWindowsStylusInputInstance() override;

		virtual bool AddEventHandler(IStylusInputEventHandler* EventHandler, EEventHandlerThread Thread) override;
		virtual bool RemoveEventHandler(IStylusInputEventHandler* EventHandler) override;

		virtual const TSharedPtr<IStylusInputTabletContext> GetTabletContext(uint32 TabletContextID) override;
		virtual const TSharedPtr<IStylusInputStylusInfo> GetStylusInfo(uint32 StylusID) override;

		virtual float GetPacketsPerSecond(EEventHandlerThread EventHandlerThread) const override;

	private:
		void Init(HWND HWindow);

		void EnablePlugin(EEventHandlerThread EventHandlerThread, IStylusInputEventHandler* EventHandler);
		void DisablePlugin(EEventHandlerThread EventHandlerThread);

		void SetupWindowContext(HWND HWindow);
		const FWindowContext& GetWindowContext() const;

		void UpdateTabletContexts(const FTabletContextContainer& InTabletContexts);

		TComPtr<IRealTimeStylus> RealTimeStylus;

		FWindowContext WindowContext;

		TUniquePtr<FWindowsStylusInputPluginAsync> AsyncPlugin;
		TUniquePtr<FWindowsStylusInputPluginSync> SyncPlugin;

		FTabletContextThreadSafeContainer TabletContexts;
		FStylusInfoThreadSafeContainer StylusInfos;

		const FWindowsStylusInputPlatformAPI& WindowsAPI;
	};
}

#endif
