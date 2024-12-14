// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Online/ExternalUICommon.h"
#include "OnlineServicesEOSGSTypes.h"

#include "eos_ui_types.h"

namespace UE::Online {

class FOnlineServicesEOSGS;

struct FExternalUIProcessDisplaySettingsUpdatedImp
{
	static constexpr TCHAR Name[] = TEXT("ProcessDisplaySettingsUpdatedImp");

	struct Params
	{
		/** True when any portion of the overlay is visible. */
		bool bIsVisible = false;
		/**
		 * True when the overlay has switched to exclusive input mode.
		 * While in exclusive input mode, no keyboard or mouse input will be sent to the game.
		 */
		bool bIsExclusiveInput = false;
	};

	struct Result
	{
	};
};

class ONLINESERVICESEOSGS_API FExternalUIEOSGS : public FExternalUICommon
{
public:
	using Super = FExternalUICommon;

	FExternalUIEOSGS(FOnlineServicesEOSGS& InOwningSubsystem);

	virtual void Initialize() override;
	virtual void PreShutdown() override;

protected:
	void RegisterEventHandlers();
	void UnregisterEventHandlers();

	void HandleDisplaySettingsUpdated(const EOS_UI_OnDisplaySettingsUpdatedCallbackInfo* Data);

	TOnlineAsyncOpHandle<FExternalUIProcessDisplaySettingsUpdatedImp> ProcessDisplaySettingsUpdatedImplOp(FExternalUIProcessDisplaySettingsUpdatedImp::Params&& Params);

	EOS_HUI UIInterfaceHandle = nullptr;
	EOSEventRegistrationPtr OnDisplaySettingsUpdated;
};

namespace Meta {

BEGIN_ONLINE_STRUCT_META(FExternalUIProcessDisplaySettingsUpdatedImp::Params)
	ONLINE_STRUCT_FIELD(FExternalUIProcessDisplaySettingsUpdatedImp::Params, bIsVisible),
	ONLINE_STRUCT_FIELD(FExternalUIProcessDisplaySettingsUpdatedImp::Params, bIsExclusiveInput)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FExternalUIProcessDisplaySettingsUpdatedImp::Result)
END_ONLINE_STRUCT_META()

/* Meta*/ }

/* UE::Online */ }
