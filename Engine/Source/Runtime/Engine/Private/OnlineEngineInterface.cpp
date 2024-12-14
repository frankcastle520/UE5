// Copyright Epic Games, Inc. All Rights Reserved.

#include "Net/OnlineEngineInterface.h"
#include "UObject/Package.h"
#include "Misc/ConfigCacheIni.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(OnlineEngineInterface)

DEFINE_LOG_CATEGORY_STATIC(LogOnlineEngine, Log, All);

UOnlineEngineInterface* UOnlineEngineInterface::Singleton = nullptr;

UOnlineEngineInterface::UOnlineEngineInterface(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

UOnlineEngineInterface* UOnlineEngineInterface::Get()
{
	if (!Singleton)
	{
		FString OnlineEngineInterfaceClassName;
		GConfig->GetString(TEXT("/Script/Engine.OnlineEngineInterface"), TEXT("ClassName"), OnlineEngineInterfaceClassName, GEngineIni);

		// To not break licensees using this, prefer this if it is present, and warn. Remove in 5.7
		bool bUseOnlineServicesV2 = false;
		if (GConfig->GetBool(TEXT("/Script/Engine.OnlineEngineInterface"), TEXT("bUseOnlineServicesV2"), bUseOnlineServicesV2, GEngineIni))
		{
			const TCHAR* V1ClassName = TEXT("/Script/OnlineSubsystemUtils.OnlineEngineInterfaceImpl");
			const TCHAR* V2ClassName = TEXT("/Script/OnlineSubsystemUtils.OnlineServicesEngineInterfaceImpl");
			OnlineEngineInterfaceClassName = bUseOnlineServicesV2 ? V2ClassName : V1ClassName;
			UE_LOG(LogOnlineEngine, Warning, TEXT("bUseOnlineServicesV2 is deprecated, please instead configure [/Script/Engine.OnlineEngineInterface]:ClassName=%s"), *OnlineEngineInterfaceClassName);
		}
		
		UClass* OnlineEngineInterfaceClass = nullptr;
		if (!OnlineEngineInterfaceClassName.IsEmpty())
		{
			OnlineEngineInterfaceClass = StaticLoadClass(UOnlineEngineInterface::StaticClass(), NULL, *OnlineEngineInterfaceClassName, NULL, LOAD_Quiet, NULL);
		}
		
		if (!OnlineEngineInterfaceClass)
		{
			// Default to the no op class if necessary
			OnlineEngineInterfaceClass = UOnlineEngineInterface::StaticClass();
		}

		Singleton = NewObject<UOnlineEngineInterface>(GetTransientPackage(), OnlineEngineInterfaceClass);
		Singleton->AddToRoot();
	}

	return Singleton;
}

