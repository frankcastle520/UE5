// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/OnlineServicesEOSGSModule.h"

#include "Modules/ModuleManager.h"
#include "Online/OnlineIdEOSGS.h"
#include "Online/OnlineServicesEOSGS.h"
#include "Online/OnlineServicesRegistry.h"
#include "Online/OnlineServicesEOSGSPlatformFactory.h"
#include "Online/SessionsEOSGS.h"

namespace UE::Online
{

class FOnlineServicesFactoryEOSGS : public IOnlineServicesFactory
{
public:
	virtual ~FOnlineServicesFactoryEOSGS() {}
	virtual TSharedPtr<IOnlineServices> Create(FName InInstanceName, FName InInstanceConfigName) override
	{
		return MakeShared<FOnlineServicesEOSGS>(InInstanceName, InInstanceConfigName);
	}
};

int FOnlineServicesEOSGSModule::GetRegistryPriority()
{
	return 0;
}

void FOnlineServicesEOSGSModule::StartupModule()
{
	FModuleManager::Get().LoadModuleChecked(TEXT("OnlineServicesInterface"));
	FModuleManager::Get().LoadModuleChecked(TEXT("EOSShared"));

	FOnlineServicesRegistry::Get().RegisterServicesFactory(EOnlineServices::Epic, MakeUnique<FOnlineServicesFactoryEOSGS>(), GetRegistryPriority());
	FOnlineIdRegistryRegistry::Get().RegisterAccountIdRegistry(EOnlineServices::Epic, &FOnlineAccountIdRegistryEOSGS::Get(), GetRegistryPriority());
	FOnlineIdRegistryRegistry::Get().RegisterSessionIdRegistry(EOnlineServices::Epic, &FOnlineSessionIdRegistryEOSGS::Get(), GetRegistryPriority());
	FOnlineIdRegistryRegistry::Get().RegisterSessionInviteIdRegistry(EOnlineServices::Epic, &FOnlineSessionInviteIdRegistryEOSGS::Get(), GetRegistryPriority());

	// Initialize the platform factory on startup.  This is necessary for the SDK to bind to rendering and input very early.
	FOnlineServicesEOSGSPlatformFactory::Get();
}

void FOnlineServicesEOSGSModule::ShutdownModule()
{
	FOnlineServicesRegistry::Get().UnregisterServicesFactory(EOnlineServices::Epic, GetRegistryPriority());
	FOnlineIdRegistryRegistry::Get().UnregisterAccountIdRegistry(EOnlineServices::Epic, GetRegistryPriority());
	FOnlineIdRegistryRegistry::Get().UnregisterSessionIdRegistry(EOnlineServices::Epic, GetRegistryPriority());
	FOnlineIdRegistryRegistry::Get().UnregisterSessionInviteIdRegistry(EOnlineServices::Epic, GetRegistryPriority());

	UE::Online::FOnlineServicesEOSGSPlatformFactory::TearDown();
}

/* UE::Online */ }

IMPLEMENT_MODULE(UE::Online::FOnlineServicesEOSGSModule, OnlineServicesEOSGS);
