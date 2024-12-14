// Copyright Epic Games, Inc. All Rights Reserved.

#include "RegularQueryService.h"

namespace UE::MultiUserClient::Replication
{
	FRegularQueryService::FRegularQueryService(const IConcertSyncClient& InOwningClient, float InInterval)
		: OwningClient(InOwningClient)
		, TickerDelegateHandle(FTSTicker::GetCoreTicker().AddTicker(
			TEXT("Multi-User Replication Query"),
			InInterval,
			[this](float)
			{
				Tick();
				return true;
			}))
		, StreamAndAuthorityQueryService(Token, OwningClient)
		, MuteStateQueryService(Token, OwningClient)
	{}

	FRegularQueryService::~FRegularQueryService()
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerDelegateHandle);
	}

	void FRegularQueryService::Tick()
	{
		StreamAndAuthorityQueryService.SendQueryEvent();
		MuteStateQueryService.SendQueryEvent();
	}
}
