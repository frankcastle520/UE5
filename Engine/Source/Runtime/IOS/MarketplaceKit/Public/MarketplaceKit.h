// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

enum class EMarketplaceType : int32
{
	AppStore = 0,
	TestFlight = 1,
	Marketplace = 2,
	Web = 3,
	Other = 4,
	NotAvailable = 5,
};

class FMarketplaceKitModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	virtual bool SupportsDynamicReloading() override;

	void GetCurrentTypeAsync(TFunction<void(EMarketplaceType Type, const FString& Name)> Callback);
	void GetCurrentType(EMarketplaceType& OutType, FString& OutName);
	FString GetCurrentTypeAsString();

private:
	bool bCachedTypeValid = false;
	EMarketplaceType CachedType = EMarketplaceType::NotAvailable;
	FString CachedName;

	void CacheValue();
};
