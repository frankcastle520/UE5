// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkMultiUserModule.h"

#include "IConcertClientTransactionBridge.h"
#include "IConcertSyncClientModule.h"
#include "LiveLinkControllerBase.h"
#include "UObject/Package.h"


namespace LiveLinkMultiUserUtils
{
	ETransactionFilterResult HandleTransactionFiltering(const FConcertTransactionFilterArgs& FilterArgs)
	{
		const UObject* ObjectToFilter = FilterArgs.ObjectToFilter;
		return ObjectToFilter && ObjectToFilter->IsA<ULiveLinkControllerBase>()
			? ETransactionFilterResult::IncludeObject
			: ETransactionFilterResult::UseDefault;
	}
}

void FLiveLinkMultiUserModule::StartupModule()
{
	if (IConcertSyncClientModule::IsAvailable())
	{
		IConcertClientTransactionBridge& TransactionBridge = IConcertSyncClientModule::Get().GetTransactionBridge();
		TransactionBridge.RegisterTransactionFilter( TEXT("LiveLinkTransactionFilter"), FOnFilterTransactionDelegate::CreateStatic(&LiveLinkMultiUserUtils::HandleTransactionFiltering));
	}
}

void FLiveLinkMultiUserModule::ShutdownModule()
{
	if (IConcertSyncClientModule::IsAvailable())
	{
		IConcertClientTransactionBridge& TransactionBridge = IConcertSyncClientModule::Get().GetTransactionBridge();
		TransactionBridge.UnregisterTransactionFilter( TEXT("LiveLinkTransactionFilter"));
	}	
}

IMPLEMENT_MODULE(FLiveLinkMultiUserModule, LiveLinkMultiUser);
