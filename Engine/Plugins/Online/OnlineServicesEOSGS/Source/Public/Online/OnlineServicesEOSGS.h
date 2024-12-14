// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Online/OnlineServicesCommon.h"

#if defined(EOS_PLATFORM_BASE_FILE_NAME)
#include EOS_PLATFORM_BASE_FILE_NAME
#endif
#include "eos_sdk.h"

#if WITH_ENGINE
class FSocketSubsystemEOS;
#endif

using IEOSPlatformHandlePtr = TSharedPtr<class IEOSPlatformHandle>;

namespace UE::Online {

class ONLINESERVICESEOSGS_API FOnlineServicesEOSGS : public FOnlineServicesCommon
{
public:
	using Super = FOnlineServicesCommon;

	FOnlineServicesEOSGS(FName InstanceName, FName InstanceConfigName);
	virtual ~FOnlineServicesEOSGS() = default;

	virtual void Init() override;
	virtual void Destroy() override;
	virtual void RegisterComponents() override;
	virtual TOnlineResult<FGetResolvedConnectString> GetResolvedConnectString(FGetResolvedConnectString::Params&& Params) override;
	virtual EOnlineServices GetServicesProvider() const override { return EOnlineServices::Epic; }

	IEOSPlatformHandlePtr GetEOSPlatformHandle() const { return EOSPlatformHandle; }

	UE_DEPRECATED(5.5, "Please call GetServiceConfigNameStatic instead")
	static const TCHAR* GetConfigNameStatic() { return GetServiceConfigNameStatic(); }
	static const TCHAR* GetServiceConfigNameStatic() { return TEXT("EOS"); }
protected:
	IEOSPlatformHandlePtr EOSPlatformHandle;

	void WarnIfEncryptionKeyMissing(const FString& InterfaceName) const;

#if WITH_ENGINE
	TSharedPtr<FSocketSubsystemEOS, ESPMode::ThreadSafe> SocketSubsystem;
#endif
};

/* UE::Online */ }
