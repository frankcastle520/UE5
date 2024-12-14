// Copyright Epic Games, Inc. All Rights Reserved.

#include "ILiveLinkHubMessagingModule.h"

#include "CoreMinimal.h"
#include "LiveLinkHubConnectionManager.h"
#include "LiveLinkHubMessageBusSourceFactory.h"
#include "LiveLinkMessageBusSourceFactory.h"
#include "Misc/ConfigCacheIni.h"
#include "Modules/ModuleManager.h"

#ifndef WITH_LIVELINK_DISCOVERY_MANAGER_THREAD
#define WITH_LIVELINK_DISCOVERY_MANAGER_THREAD 1
#endif


class FLiveLinkHubMessagingModule : public ILiveLinkHubMessagingModule
{
public:
	//~ Begin IModuleInterface
	virtual void StartupModule() override
	{
		// The connection manager is used to communicate with the hub,
		// so we don't need it when we're running the hub itself.
		bUseConnectionManager = GConfig->GetBoolOrDefault(
			TEXT("LiveLinkHub"), TEXT("bUseMessagingConnectionManager"), true, GEngineIni);

#if WITH_LIVELINK_DISCOVERY_MANAGER_THREAD
		if (bUseConnectionManager)
		{
			ConnectionManager = MakeUnique<FLiveLinkHubConnectionManager>();
		}
#endif

		SourceFilterDelegate = ILiveLinkModule::Get().RegisterMessageBusSourceFilter(FOnLiveLinkShouldDisplaySource::CreateRaw(this, &FLiveLinkHubMessagingModule::OnFilterMessageBusSource));
	}

	virtual void ShutdownModule() override
	{
		if (ILiveLinkModule* LiveLinkModule = FModuleManager::Get().GetModulePtr<ILiveLinkModule>("LiveLink"))
		{
			LiveLinkModule->UnregisterMessageBusSourceFilter(SourceFilterDelegate);
		}

#if WITH_LIVELINK_DISCOVERY_MANAGER_THREAD
		ConnectionManager.Reset();
#endif
	}
	//~ End IModuleInterface

	//~ ILiveLinkHubMessagingModule interface
	virtual FOnHubConnectionEstablished& OnConnectionEstablished() override
	{
		return ConnectionEstablishedDelegate;
	}

private:
	/** Filter invoked by the messagebus source factory to filter out sources in the creation panel. */
	bool OnFilterMessageBusSource(UClass* FactoryClass, FProviderPollResultPtr PollResult) const
	{
		if (!PollResult)
		{
			return false;
		}

		const bool bIsLiveLinkHubProvider = PollResult->Annotations.FindRef(FLiveLinkHubMessageAnnotation::ProviderTypeAnnotation) == UE::LiveLinkHub::Private::LiveLinkHubProviderType;

		if (!bUseConnectionManager)
		{
			// When running the hub
			if (FactoryClass == ULiveLinkMessageBusSourceFactory::StaticClass())
			{
				// Don't show the livelink hub in the message bus source discovery. (Should be changed in the future if we want to allow hubs to speak to each others).
				return !bIsLiveLinkHubProvider;
			}
		}
		else
		{
			// When running in UE
			if (FactoryClass == ULiveLinkHubMessageBusSourceFactory::StaticClass())
			{
				return bIsLiveLinkHubProvider;
			}
			else if (FactoryClass == ULiveLinkMessageBusSourceFactory::StaticClass())
			{
				return !bIsLiveLinkHubProvider;
			}
		}

		return true;
	}

	bool bUseConnectionManager;

#if WITH_LIVELINK_DISCOVERY_MANAGER_THREAD
	/** Manages the connection to the live link hub. */
	TUniquePtr<FLiveLinkHubConnectionManager> ConnectionManager;
#endif

	/** Handle to the delegate used to filter message bus sources. */
	FDelegateHandle SourceFilterDelegate;

	/** Delegate called when the connection between a livelink hub and the editor is established. */
	FOnHubConnectionEstablished ConnectionEstablishedDelegate;
};


IMPLEMENT_MODULE(FLiveLinkHubMessagingModule, LiveLinkHubMessaging);
