// Copyright Epic Games, Inc. All Rights Reserved.

#include "MarketplaceKit.h"
#include "MarketplaceKitWrapper.h"
#include "Logging/LogMacros.h"

DEFINE_LOG_CATEGORY_STATIC(LogMarketplaceKit, Log, All);

void FMarketplaceKitModule::StartupModule()
{
	CacheValue();
}

void FMarketplaceKitModule::ShutdownModule()
{
}

bool FMarketplaceKitModule::SupportsDynamicReloading()
{
	return true;
}

static constexpr EMarketplaceType ConvertMarketplaceType(const AppDistributorType Type)
{
	switch (Type)
	{
		case AppDistributorTypeAppStore:		return EMarketplaceType::AppStore;
		case AppDistributorTypeTestFlight:		return EMarketplaceType::TestFlight;
		case AppDistributorTypeMarketplace:		return EMarketplaceType::Marketplace;
		case AppDistributorTypeWeb:				return EMarketplaceType::Web;
		case AppDistributorTypeOther:			return EMarketplaceType::Other;
		case AppDistributorTypeNotAvailable:	return EMarketplaceType::NotAvailable;
		default:								return EMarketplaceType::Other;
	}
}

void FMarketplaceKitModule::GetCurrentTypeAsync(TFunction<void(EMarketplaceType Type, const FString& Name)> Callback)
{
	[AppDistributorWrapper getCurrentWithCompletionHandler:^(enum AppDistributorType Type, NSString* _Nonnull Name)
	{
		const EMarketplaceType ConvertedType = ConvertMarketplaceType(Type);
		const FString ConvertedName(Name);

		UE_LOG(LogMarketplaceKit, Log, TEXT("AppDistributorWrapper getCurrentWithCompletionHandler %i %s"), (int32)ConvertedType, *ConvertedName);

		Callback(ConvertedType, ConvertedName);
	}];
}

void FMarketplaceKitModule::GetCurrentType(EMarketplaceType& OutType, FString& OutName)
{
	CacheValue();
	OutType = CachedType;
	OutName = CachedName;
}

FString FMarketplaceKitModule::GetCurrentTypeAsString()
{
	CacheValue();
	
	TStringBuilder<256> Result;
	switch (CachedType)
	{
		case EMarketplaceType::AppStore:		Result.Append(TEXT("AppStore")); break;
		case EMarketplaceType::TestFlight:		Result.Append(TEXT("TestFlight")); break;
		case EMarketplaceType::Marketplace:		Result.Append(TEXT("Marketplace")); break;
		case EMarketplaceType::Web:				Result.Append(TEXT("Web")); break;
		case EMarketplaceType::NotAvailable:	Result.Append(TEXT("NotAvailable")); break;
		case EMarketplaceType::Other:			[[fallthrough]];
		default:								Result.Append(TEXT("Other")); break;
	}
	
	if (!CachedName.IsEmpty())
	{
		Result.Append(TEXT("-"));
		Result.Append(CachedName);
	}

	return *Result;
}

void FMarketplaceKitModule::CacheValue()
{
	if (bCachedTypeValid)
	{
		return;
	}

	// TODO avoid scheduling multiple requests in case this path is hit from multiple threads

	dispatch_semaphore_t Semaphore = dispatch_semaphore_create(0);

	[AppDistributorWrapper getCurrentWithCompletionHandler:^(enum AppDistributorType Type, NSString* _Nonnull Name)
	{
		CachedType = ConvertMarketplaceType(Type);
		CachedName = Name;
		bCachedTypeValid = true;

		UE_LOG(LogMarketplaceKit, Log, TEXT("AppDistributorWrapper getCurrentWithCompletionHandler %i %s"), (int32)CachedType, *CachedName);

		dispatch_semaphore_signal(Semaphore);
	}];

	// wait for a result, but timeout after 1s
	dispatch_semaphore_wait(Semaphore, dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC));
    dispatch_release(Semaphore);
}

IMPLEMENT_MODULE(FMarketplaceKitModule, MarketplaceKit);
